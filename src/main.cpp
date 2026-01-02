import Core.Prelude;
import Core.Entrance;
import Vendor.ApplicationAPI;
import "SDL3/SDL.h";

// Simple message callback for NVRHI
class NvrhiMessageCallback : public nvrhi::IMessageCallback {
public:
    void message(nvrhi::MessageSeverity severity, const char *messageText) override {
        const char *severityStr = "";
        switch (severity) {
            case nvrhi::MessageSeverity::Info: severityStr = "INFO";
                break;
            case nvrhi::MessageSeverity::Warning: severityStr = "WARNING";
                break;
            case nvrhi::MessageSeverity::Error: severityStr = "ERROR";
                break;
            case nvrhi::MessageSeverity::Fatal: severityStr = "FATAL";
                break;
        }
        std::cerr << "[NVRHI " << severityStr << "] " << messageText << std::endl;
    }
};

struct SwapChainData {
    vk::SharedSwapchainKHR swapchain;
    std::vector<vk::SharedImage> swapchainImages;
    std::vector<nvrhi::TextureHandle> backBuffers;
    std::vector<nvrhi::FramebufferHandle> framebuffers;
    uint32_t width = 0;
    uint32_t height = 0;
};

// Application class with all inline implementations
class Application {
public:
    Application() = default;

    ~Application() {
        Destroy();
    }

    // Non-copyable
    Application(const Application &) = delete;

    Application &operator=(const Application &) = delete;

    // Getter methods
    [[nodiscard]] const std::shared_ptr<SDL_Window> &GetWindow() const { return mWindow; }

    [[nodiscard]] const vk::SharedInstance &GetVkInstance() const { return mVkInstance; }
    [[nodiscard]] const vk::SharedPhysicalDevice &GetVkPhysicalDevice() const { return mVkPhysicalDevice; }
    [[nodiscard]] const vk::SharedSurfaceKHR &GetVkSurface() const { return mVkSurface; }
    [[nodiscard]] const vk::SharedDevice &GetVkDevice() const { return mVkDevice; }
    [[nodiscard]] const vk::SharedQueue &GetVkQueue() const { return mVkQueue; }

    [[nodiscard]] const nvrhi::DeviceHandle &GetNvrhiDevice() const { return mNvrhiDevice; }
    [[nodiscard]] const nvrhi::CommandListHandle &GetCommandList() const { return mCommandList; }

    [[nodiscard]] const SwapChainData &GetSwapchainData() const { return mSwapchainData; }

    [[nodiscard]] bool IsRunning() const { return mRunning; }
    [[nodiscard]] bool IsMinimized() const { return mMinimized; }

    void Init() {
        CreateWindow();
        InitVulkan();
        CreateVulkanInstance();
        SelectPhysicalDevice();
        CreateSurface();
        CreateLogicalDevice();
        InitNVRHI();
        CreateSwapchain();
        CreateSyncObjects();

        mCommandList = mNvrhiDevice->createCommandList();
    }

    void Run() {
        mRunning = true;

        while (mRunning) {
            ProcessEvents();

            if (mNeedsResize) {
                RecreateSwapchain();
                mNeedsResize = false;
                mCurrentFrame = 0;
                continue;
            }

            if (!mMinimized)
                RenderFrame();
        }
    }

    void Destroy() {
        if (!mVkDevice) return;

        mVkDevice->waitIdle();

        // 1. Clear NVRHI resources
        mSwapchainData.framebuffers.clear();
        mSwapchainData.backBuffers.clear();
        mSwapchainData.swapchainImages.clear();
        mSwapchainData.swapchain = vk::SharedSwapchainKHR{};

        mCommandList = nullptr;

        // 2. Destroy NVRHI device (needs Vulkan device to clean up)
        mNvrhiDevice = nullptr;

        // 3. Clear Vulkan synchronization objects
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
            mRenderCompleteFences[i].reset();
        }
        mAcquireSemaphores.clear();
        mRenderCompleteSemaphores.clear();

        // 5. Destroy debug messenger
        if (mDebugMessenger) {
            mVkInstance.get().destroyDebugUtilsMessengerEXT(mDebugMessenger);
            mDebugMessenger = nullptr;
        }

        // 6. Clear Vulkan objects (in reverse order of creation)
        mVkQueue.reset();
        mVkDevice.reset();
        mVkSurface.reset();
        mVkPhysicalDevice.reset();
        mVkInstance.reset();

        // 7. Destroy window last
        mWindow.reset();
    }

private:
    void CreateWindow() {
        mWindow = std::shared_ptr<SDL_Window>(SDL_CreateWindow("NVRHI Vulkan Application", 1280, 720,
                                                               SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE),
                                              SDL_DestroyWindow);
        if (!mWindow) {
            throw Engine::RuntimeException(std::format("Failed to create window: {}", SDL_GetError()));
        }
    }

    void InitVulkan() {
        auto vkGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
            SDL_Vulkan_GetVkGetInstanceProcAddr());
        if (!vkGetInstanceProcAddr) {
            throw Engine::RuntimeException("Failed to get Vulkan instance proc addr from SDL");
        }

        vk::detail::defaultDispatchLoaderDynamic.init(vkGetInstanceProcAddr);
    }

    void CreateVulkanInstance() {
        uint32_t extCount = 0;
        const char *const*extensions = SDL_Vulkan_GetInstanceExtensions(&extCount);
        std::vector<const char *> instanceExtensions(extensions, extensions + extCount);

        // Enable Vulkan validation layers
        const char *validationLayers[] = {
            "VK_LAYER_KHRONOS_validation"
        };

        // Add debug utils extension for validation messages
        instanceExtensions.push_back(vk::EXTDebugUtilsExtensionName);

        constexpr bool enableValidation = true;

        vk::ApplicationInfo appInfo;
        appInfo.apiVersion = vk::ApiVersion12;

        vk::InstanceCreateInfo instInfo;
        instInfo.pApplicationInfo = &appInfo;
        instInfo.enabledExtensionCount = static_cast<uint32_t>(instanceExtensions.size());
        instInfo.ppEnabledExtensionNames = instanceExtensions.data();

        if (enableValidation) {
            instInfo.enabledLayerCount = 1;
            instInfo.ppEnabledLayerNames = validationLayers;
        }

        vk::Instance instance = vk::createInstance(instInfo);
        mVkInstance = vk::SharedInstance(instance);
        vk::detail::defaultDispatchLoaderDynamic.init(mVkInstance.get());

        // Setup debug messenger to output errors to stderr
        if (enableValidation) {
            SetupDebugMessenger();
        }
    }

    void SetupDebugMessenger() {
        vk::DebugUtilsMessengerCreateInfoEXT createInfo;
        createInfo.messageSeverity =
                vk::DebugUtilsMessageSeverityFlagBitsEXT::eError |
                vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning;
        createInfo.messageType =
                vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
                vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
                vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance;
        createInfo.pfnUserCallback = reinterpret_cast<vk::PFN_DebugUtilsMessengerCallbackEXT>(
            reinterpret_cast<void *>(DebugCallback));

        mDebugMessenger = mVkInstance.get().createDebugUtilsMessengerEXT(createInfo);
    }

    static vk::Bool32 DebugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
        VkDebugUtilsMessageTypeFlagsEXT messageType,
        const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
        void *pUserData) {
        // Output to stderr for errors and warnings
        if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
            std::cerr << "Validation Error: " << pCallbackData->pMessage << std::endl;
#ifdef _DEBUG
            __debugbreak();
#endif
        } else if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
            std::cerr << "Validation Warning: " << pCallbackData->pMessage << std::endl;
        }

        return vk::False;
    }


    void SelectPhysicalDevice() {
        std::vector<vk::PhysicalDevice> physicalDevices = mVkInstance.get().enumeratePhysicalDevices();
        if (physicalDevices.empty()) {
            throw Engine::RuntimeException("No Vulkan-capable GPU found");
        }
        mVkPhysicalDevice = vk::SharedPhysicalDevice(physicalDevices[0], mVkInstance);
    }

    void CreateSurface() {
        VkSurfaceKHR rawSurface;
        if (!SDL_Vulkan_CreateSurface(mWindow.get(), mVkInstance.get(), nullptr, &rawSurface)) {
            throw Engine::RuntimeException("Failed to create Vulkan surface");
        }
        mVkSurface = vk::SharedSurfaceKHR(vk::SurfaceKHR(rawSurface), mVkInstance);
    }

    void CreateLogicalDevice() {
        float queuePriority = 1.0f;
        vk::DeviceQueueCreateInfo queueInfo;
        queueInfo.queueFamilyIndex = 0;
        queueInfo.queueCount = 1;
        queueInfo.pQueuePriorities = &queuePriority;

        const char *deviceExtensions[] = {vk::KHRSwapchainExtensionName};

        vk::PhysicalDeviceVulkan12Features features12;
        features12.descriptorIndexing = vk::True;
        features12.bufferDeviceAddress = vk::True;
        features12.runtimeDescriptorArray = vk::True;
        features12.shaderSampledImageArrayNonUniformIndexing = vk::True;
        features12.descriptorBindingPartiallyBound = vk::True;
        features12.timelineSemaphore = vk::True; // Enable timeline semaphore for NVRHI

        vk::DeviceCreateInfo devInfo;
        devInfo.pNext = &features12;
        devInfo.queueCreateInfoCount = 1;
        devInfo.pQueueCreateInfos = &queueInfo;
        devInfo.enabledExtensionCount = 1;
        devInfo.ppEnabledExtensionNames = deviceExtensions;

        vk::Device device = mVkPhysicalDevice.get().createDevice(devInfo);
        mVkDevice = vk::SharedDevice(device);
        vk::detail::defaultDispatchLoaderDynamic.init(mVkInstance.get(), mVkDevice.get());

        vk::Queue queue = mVkDevice.get().getQueue(0, 0);
        mVkQueue = vk::SharedQueue(queue, mVkDevice);
    }

    void InitNVRHI() {
        mMessageCallback = std::make_shared<NvrhiMessageCallback>();

        const char *deviceExtensions[] = {vk::KHRSwapchainExtensionName};

        nvrhi::vulkan::DeviceDesc nvrhiDesc;
        nvrhiDesc.errorCB = mMessageCallback.get();
        nvrhiDesc.instance = mVkInstance.get();
        nvrhiDesc.physicalDevice = mVkPhysicalDevice.get();
        nvrhiDesc.device = mVkDevice.get();
        nvrhiDesc.graphicsQueue = mVkQueue.get();
        nvrhiDesc.graphicsQueueIndex = 0;
        nvrhiDesc.deviceExtensions = deviceExtensions;
        nvrhiDesc.numDeviceExtensions = 1;

        mNvrhiDevice = nvrhi::vulkan::createDevice(nvrhiDesc);
    }

    void CreateSwapchain() {
        mSwapchainData = CreateSwapchainInternal();

        // Create acquire semaphores per frame in flight
        mAcquireSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
        vk::SemaphoreCreateInfo semaphoreInfo;
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
            vk::Semaphore acquireSem = mVkDevice.get().createSemaphore(semaphoreInfo);
            mAcquireSemaphores[i] = vk::SharedHandle<vk::Semaphore>(acquireSem, mVkDevice);
        }

        // Create render complete semaphores per swapchain image
        const uint32_t imageCount = static_cast<uint32_t>(mSwapchainData.backBuffers.size());
        mRenderCompleteSemaphores.resize(imageCount);
        for (uint32_t i = 0; i < imageCount; ++i) {
            vk::Semaphore renderSem = mVkDevice.get().createSemaphore(semaphoreInfo);
            mRenderCompleteSemaphores[i] = vk::SharedHandle<vk::Semaphore>(renderSem, mVkDevice);
        }
    }

    void CreateSyncObjects() {
        // Create Fences for frame synchronization
        vk::FenceCreateInfo fenceInfo;
        fenceInfo.flags = vk::FenceCreateFlagBits::eSignaled; // Start signaled so first frame doesn't wait

        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
            vk::Fence fence = mVkDevice.get().createFence(fenceInfo);
            mRenderCompleteFences[i] = vk::SharedFence(fence, mVkDevice);
        }
    }

    void RecreateSwapchain() {
        // Wait for all in-flight frames to complete
        std::vector<vk::Fence> fencesToWait;
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
            fencesToWait.push_back(mRenderCompleteFences[i].get());
        }

        vk::Result waitResult = mVkDevice.get().waitForFences(
            fencesToWait,
            vk::True,
            UINT64_MAX
        );

        if (waitResult != vk::Result::eSuccess) {
            throw Engine::RuntimeException("Failed to wait for fences during swapchain recreation");
        }

        vk::SwapchainKHR oldSwapchain = mSwapchainData.swapchain ? mSwapchainData.swapchain.get() : nullptr;
        mSwapchainData = CreateSwapchainInternal(oldSwapchain);

        mNvrhiDevice->waitForIdle();

        // Clear old semaphores
        mAcquireSemaphores.clear();
        mRenderCompleteSemaphores.clear();

        // Recreate acquire semaphores per frame in flight
        mAcquireSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
        vk::SemaphoreCreateInfo semaphoreInfo;
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
            vk::Semaphore acquireSem = mVkDevice.get().createSemaphore(semaphoreInfo);
            mAcquireSemaphores[i] = vk::SharedHandle<vk::Semaphore>(acquireSem, mVkDevice);
        }

        // Recreate render complete semaphores per swapchain image
        const uint32_t imageCount = static_cast<uint32_t>(mSwapchainData.backBuffers.size());
        mRenderCompleteSemaphores.resize(imageCount);
        for (uint32_t i = 0; i < imageCount; ++i) {
            vk::Semaphore renderSem = mVkDevice.get().createSemaphore(semaphoreInfo);
            mRenderCompleteSemaphores[i] = vk::SharedHandle<vk::Semaphore>(renderSem, mVkDevice);
        }
    }

    SwapChainData CreateSwapchainInternal(vk::SwapchainKHR oldSwapchain = nullptr) {
        SwapChainData result;

        int width, height;
        SDL_GetWindowSizeInPixels(mWindow.get(), &width, &height);
        result.width = static_cast<uint32_t>(width);
        result.height = static_cast<uint32_t>(height);

        vk::SurfaceCapabilitiesKHR caps = mVkPhysicalDevice.get().getSurfaceCapabilitiesKHR(mVkSurface.get());

        vk::SwapchainCreateInfoKHR swapchainInfo;
        swapchainInfo.surface = mVkSurface.get();
        swapchainInfo.minImageCount = 2;
        swapchainInfo.imageFormat = vk::Format::eB8G8R8A8Unorm;
        swapchainInfo.imageColorSpace = vk::ColorSpaceKHR::eSrgbNonlinear;
        swapchainInfo.imageExtent = vk::Extent2D(result.width, result.height);
        swapchainInfo.imageArrayLayers = 1;
        swapchainInfo.imageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferDst;
        swapchainInfo.preTransform = caps.currentTransform;
        swapchainInfo.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
        swapchainInfo.presentMode = vk::PresentModeKHR::eFifo;
        swapchainInfo.clipped = vk::True;
        swapchainInfo.oldSwapchain = oldSwapchain;

        vk::SwapchainKHR swapchain = mVkDevice.get().createSwapchainKHR(swapchainInfo);
        result.swapchain = vk::SharedSwapchainKHR(swapchain, mVkDevice, mVkSurface);

        std::vector<vk::Image> rawImages = mVkDevice.get().getSwapchainImagesKHR(swapchain);

        for (const auto &img: rawImages) {
            vk::SharedImage sharedImg(img, mVkDevice, vk::SwapchainOwns::yes);
            result.swapchainImages.push_back(sharedImg);

            auto textureDesc = nvrhi::TextureDesc()
                    .setDimension(nvrhi::TextureDimension::Texture2D)
                    .setFormat(nvrhi::Format::BGRA8_UNORM)
                    .setWidth(result.width)
                    .setHeight(result.height)
                    .setIsRenderTarget(true)
                    .setDebugName("BackBuffer")
                    .setInitialState(nvrhi::ResourceStates::Present)
                    .setKeepInitialState(true);

            nvrhi::TextureHandle handle = mNvrhiDevice->createHandleForNativeTexture(
                nvrhi::ObjectTypes::VK_Image,
                nvrhi::Object(img),
                textureDesc
            );
            result.backBuffers.push_back(handle);

            auto framebufferDesc = nvrhi::FramebufferDesc().addColorAttachment(handle);
            nvrhi::FramebufferHandle framebuffer = mNvrhiDevice->createFramebuffer(framebufferDesc);
            result.framebuffers.push_back(framebuffer);
        }

        return result;
    }

    void ProcessEvents() {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                mRunning = false;
            }
            if (event.type == SDL_EVENT_WINDOW_RESIZED ||
                event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
                mNeedsResize = true;
            }

            if (event.type == SDL_EVENT_WINDOW_MINIMIZED) {
                mMinimized = true;
            } else if (event.type == SDL_EVENT_WINDOW_RESTORED) {
                mMinimized = false;
            }
        }
    }

    void RenderFrame() {
        vk::SharedFence &currentRenderCompleteFence = mRenderCompleteFences[mCurrentFrame];

        // Wait for this frame's previous work to complete
        vk::Result waitResult = mVkDevice.get().waitForFences(
            currentRenderCompleteFence.get(),
            vk::True,
            UINT64_MAX
        );

        if (waitResult != vk::Result::eSuccess) {
            throw Engine::RuntimeException("Failed to wait for render complete fence");
        }

        // Use per-frame acquire semaphore
        vk::SharedHandle<vk::Semaphore> &frameAcquireSemaphore = mAcquireSemaphores[mCurrentFrame];

        // Acquire next swapchain image
        uint32_t imageIndex;
        vk::Result res = mVkDevice.get().acquireNextImageKHR(
            mSwapchainData.swapchain.get(),
            UINT64_MAX,
            frameAcquireSemaphore.get(),
            nullptr,
            &imageIndex
        );

        if (res == vk::Result::eErrorOutOfDateKHR || res == vk::Result::eSuboptimalKHR) {
            mNeedsResize = true;
            return;
        }

        if (res != vk::Result::eSuccess) {
            throw Engine::RuntimeException("Failed to acquire swapchain image");
        }

        // Reset fence before submitting new work
        mVkDevice.get().resetFences(currentRenderCompleteFence.get());

        // Use per-image render complete semaphore
        vk::SharedHandle<vk::Semaphore> &imageRenderCompleteSemaphore = mRenderCompleteSemaphores[imageIndex];

        mCommandList->open();

        const nvrhi::FramebufferHandle &currentFramebuffer = mSwapchainData.framebuffers[imageIndex];
        nvrhi::Color purple(1.0f, 0.0f, 1.0f, 1.0f);
        nvrhi::utils::ClearColorAttachment(mCommandList, currentFramebuffer, 0, purple);

        mCommandList->close();

        mNvrhiDevice->queueWaitForSemaphore(nvrhi::CommandQueue::Graphics, frameAcquireSemaphore.get(), 0);
        mNvrhiDevice->queueSignalSemaphore(nvrhi::CommandQueue::Graphics, imageRenderCompleteSemaphore.get(), 0);

        mNvrhiDevice->executeCommandListSignalFence(mCommandList, currentRenderCompleteFence.get());

        // Present
        vk::SwapchainKHR rawSwapchain = mSwapchainData.swapchain.get();
        vk::PresentInfoKHR presentInfo{};
        presentInfo.waitSemaphoreCount = 1;
        vk::Semaphore waitSemaphores[] = {imageRenderCompleteSemaphore.get()};
        presentInfo.pWaitSemaphores = waitSemaphores;
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &rawSwapchain;
        presentInfo.pImageIndices = &imageIndex;

        res = mVkQueue.get().presentKHR(presentInfo);
        if (res == vk::Result::eErrorOutOfDateKHR || res == vk::Result::eSuboptimalKHR) {
            mNeedsResize = true;
        }

        mCurrentFrame = (mCurrentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    }

private:
    // Member variables (order matters for destruction)
    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 3;

    // Window
    std::shared_ptr<SDL_Window> mWindow;

    // Vulkan objects
    vk::SharedInstance mVkInstance;
    vk::DebugUtilsMessengerEXT mDebugMessenger; // Debug messenger for validation
    vk::SharedPhysicalDevice mVkPhysicalDevice;
    vk::SharedSurfaceKHR mVkSurface;
    vk::SharedDevice mVkDevice;
    vk::SharedQueue mVkQueue;

    // NVRHI
    std::shared_ptr<NvrhiMessageCallback> mMessageCallback;
    nvrhi::vulkan::DeviceHandle mNvrhiDevice;
    nvrhi::CommandListHandle mCommandList;

    // Swapchain
    SwapChainData mSwapchainData;

    // Synchronization
    std::vector<vk::SharedHandle<vk::Semaphore>> mAcquireSemaphores; // Per-frame (for acquire)
    std::vector<vk::SharedHandle<vk::Semaphore>> mRenderCompleteSemaphores; // Per-swapchain-image
    std::array<vk::SharedFence, MAX_FRAMES_IN_FLIGHT> mRenderCompleteFences;
    uint32_t mCurrentFrame = 0;

    // State
    bool mRunning = false;
    bool mNeedsResize = false;
    bool mMinimized = false;
};


namespace
Engine {
    int Main(int argc, char **argv) {
        Application app;
        app.Init();
        app.Run();
        app.Destroy();
        return 0;
    }
}
