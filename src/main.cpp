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
    [[nodiscard]] const std::shared_ptr<SDL_Window> &GetWindow() const { return m_Window; }

    [[nodiscard]] const vk::SharedInstance &GetVkInstance() const { return m_VkInstance; }
    [[nodiscard]] const vk::SharedPhysicalDevice &GetVkPhysicalDevice() const { return m_VkPhysicalDevice; }
    [[nodiscard]] const vk::SharedSurfaceKHR &GetVkSurface() const { return m_VkSurface; }
    [[nodiscard]] const vk::SharedDevice &GetVkDevice() const { return m_VkDevice; }
    [[nodiscard]] const vk::SharedQueue &GetVkQueue() const { return m_VkQueue; }

    [[nodiscard]] const nvrhi::DeviceHandle &GetNvrhiDevice() const { return m_NvrhiDevice; }
    [[nodiscard]] const nvrhi::CommandListHandle &GetCommandList() const { return m_CommandList; }

    [[nodiscard]] const SwapChainData &GetSwapchainData() const { return m_SwapchainData; }

    [[nodiscard]] bool IsRunning() const { return m_Running; }

    bool Init() {
        if (!CreateWindow()) return false;
        if (!InitVulkan()) return false;
        if (!CreateVulkanInstance()) return false;
        if (!SelectPhysicalDevice()) return false;
        if (!CreateSurface()) return false;
        if (!CreateLogicalDevice()) return false;
        if (!InitNVRHI()) return false;
        if (!CreateSwapchain()) return false;
        if (!CreateSyncObjects()) return false;

        m_CommandList = m_NvrhiDevice->createCommandList();

        return true;
    }

    void Run() {
        m_Running = true;

        while (m_Running) {
            ProcessEvents();

            if (m_NeedsResize) {
                RecreateSwapchain();
                m_NeedsResize = false;
                m_CurrentFrame = 0;
                continue;
            }

            RenderFrame();
        }
    }

    void Destroy() {
        if (!m_VkDevice) return;

        m_VkDevice->waitIdle();

        // 1. Clear NVRHI resources
        m_SwapchainData.framebuffers.clear();
        m_SwapchainData.backBuffers.clear();
        m_SwapchainData.swapchainImages.clear();
        m_SwapchainData.swapchain = vk::SharedSwapchainKHR{};

        // 2. Clear EventQueries
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
            m_RenderCompleteEvents[i] = nullptr;
        }

        m_CommandList = nullptr;

        // 3. Destroy NVRHI device (needs Vulkan device to clean up)
        m_NvrhiDevice = nullptr;

        // 4. Clear Vulkan synchronization objects
        m_AcquireSemaphores.clear();
        m_RenderCompleteSemaphores.clear();

        // 5. Destroy debug messenger
        if (m_DebugMessenger) {
            m_VkInstance.get().destroyDebugUtilsMessengerEXT(m_DebugMessenger);
            m_DebugMessenger = nullptr;
        }

        // 6. Clear Vulkan objects (in reverse order of creation)
        m_VkQueue.reset();
        m_VkDevice.reset();
        m_VkSurface.reset();
        m_VkPhysicalDevice.reset();
        m_VkInstance.reset();

        // 7. Destroy window last
        m_Window.reset();
    }

private:
    bool CreateWindow() {
        m_Window = std::shared_ptr<SDL_Window>(SDL_CreateWindow("NVRHI Vulkan Application", 1280, 720,
                                                                SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE),
                                               SDL_DestroyWindow);
        if (!m_Window) {
            std::cerr << "Failed to create window: " << SDL_GetError() << std::endl;
            return false;
        }
        return true;
    }

    bool InitVulkan() {
        auto vkGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
            SDL_Vulkan_GetVkGetInstanceProcAddr());
        if (!vkGetInstanceProcAddr) {
            std::cerr << "Failed to get Vulkan instance proc addr from SDL" << std::endl;
            return false;
        }

        vk::detail::defaultDispatchLoaderDynamic.init(vkGetInstanceProcAddr);
        return true;
    }

    bool CreateVulkanInstance() {
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
        m_VkInstance = vk::SharedInstance(instance);
        vk::detail::defaultDispatchLoaderDynamic.init(m_VkInstance.get());

        // Setup debug messenger to output errors to stderr
        if (enableValidation) {
            SetupDebugMessenger();
        }

        return true;
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

        m_DebugMessenger = m_VkInstance.get().createDebugUtilsMessengerEXT(createInfo);
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


    bool SelectPhysicalDevice() {
        std::vector<vk::PhysicalDevice> physicalDevices = m_VkInstance.get().enumeratePhysicalDevices();
        if (physicalDevices.empty()) {
            std::cerr << "No Vulkan-capable GPU found" << std::endl;
            return false;
        }
        m_VkPhysicalDevice = vk::SharedPhysicalDevice(physicalDevices[0], m_VkInstance);
        return true;
    }

    bool CreateSurface() {
        VkSurfaceKHR rawSurface;
        if (!SDL_Vulkan_CreateSurface(m_Window.get(), m_VkInstance.get(), nullptr, &rawSurface)) {
            std::cerr << "Failed to create Vulkan surface" << std::endl;
            return false;
        }
        m_VkSurface = vk::SharedSurfaceKHR(vk::SurfaceKHR(rawSurface), m_VkInstance);
        return true;
    }

    bool CreateLogicalDevice() {
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

        vk::Device device = m_VkPhysicalDevice.get().createDevice(devInfo);
        m_VkDevice = vk::SharedDevice(device);
        vk::detail::defaultDispatchLoaderDynamic.init(m_VkInstance.get(), m_VkDevice.get());

        vk::Queue queue = m_VkDevice.get().getQueue(0, 0);
        m_VkQueue = vk::SharedQueue(queue, m_VkDevice);

        return true;
    }

    bool InitNVRHI() {
        m_MessageCallback = std::make_shared<NvrhiMessageCallback>();

        const char *deviceExtensions[] = {vk::KHRSwapchainExtensionName};

        nvrhi::vulkan::DeviceDesc nvrhiDesc;
        nvrhiDesc.errorCB = m_MessageCallback.get();
        nvrhiDesc.instance = m_VkInstance.get();
        nvrhiDesc.physicalDevice = m_VkPhysicalDevice.get();
        nvrhiDesc.device = m_VkDevice.get();
        nvrhiDesc.graphicsQueue = m_VkQueue.get();
        nvrhiDesc.graphicsQueueIndex = 0;
        nvrhiDesc.deviceExtensions = deviceExtensions;
        nvrhiDesc.numDeviceExtensions = 1;

        m_NvrhiDevice = nvrhi::vulkan::createDevice(nvrhiDesc);

        return true;
    }

    bool CreateSwapchain() {
        m_SwapchainData = CreateSwapchainInternal();

        // Create acquire semaphores per frame in flight
        m_AcquireSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
        vk::SemaphoreCreateInfo semaphoreInfo;
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
            vk::Semaphore acquireSem = m_VkDevice.get().createSemaphore(semaphoreInfo);
            m_AcquireSemaphores[i] = vk::SharedHandle<vk::Semaphore>(acquireSem, m_VkDevice);
        }

        // Create render complete semaphores per swapchain image
        const uint32_t imageCount = static_cast<uint32_t>(m_SwapchainData.backBuffers.size());
        m_RenderCompleteSemaphores.resize(imageCount);
        for (uint32_t i = 0; i < imageCount; ++i) {
            vk::Semaphore renderSem = m_VkDevice.get().createSemaphore(semaphoreInfo);
            m_RenderCompleteSemaphores[i] = vk::SharedHandle<vk::Semaphore>(renderSem, m_VkDevice);
        }

        return true;
    }

    bool CreateSyncObjects() {
        // Create EventQueries for frame synchronization
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
            m_RenderCompleteEvents[i] = m_NvrhiDevice->createEventQuery();
        }
        return true;
    }

    void RecreateSwapchain() {
        // Wait for all in-flight frames to complete
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
            m_NvrhiDevice->waitEventQuery(m_RenderCompleteEvents[i]);
        }

        // Clear old semaphores
        m_AcquireSemaphores.clear();
        m_RenderCompleteSemaphores.clear();

        vk::SwapchainKHR oldSwapchain = m_SwapchainData.swapchain ? m_SwapchainData.swapchain.get() : nullptr;
        m_SwapchainData = CreateSwapchainInternal(oldSwapchain);

        // Recreate acquire semaphores per frame in flight
        m_AcquireSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
        vk::SemaphoreCreateInfo semaphoreInfo;
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
            vk::Semaphore acquireSem = m_VkDevice.get().createSemaphore(semaphoreInfo);
            m_AcquireSemaphores[i] = vk::SharedHandle<vk::Semaphore>(acquireSem, m_VkDevice);
        }

        // Recreate render complete semaphores per swapchain image
        const uint32_t imageCount = static_cast<uint32_t>(m_SwapchainData.backBuffers.size());
        m_RenderCompleteSemaphores.resize(imageCount);
        for (uint32_t i = 0; i < imageCount; ++i) {

            vk::Semaphore renderSem = m_VkDevice.get().createSemaphore(semaphoreInfo);
            m_RenderCompleteSemaphores[i] = vk::SharedHandle<vk::Semaphore>(renderSem, m_VkDevice);
        }
    }

    SwapChainData CreateSwapchainInternal(vk::SwapchainKHR oldSwapchain = nullptr) {
        SwapChainData result;

        int width, height;
        SDL_GetWindowSizeInPixels(m_Window.get(), &width, &height);
        result.width = static_cast<uint32_t>(width);
        result.height = static_cast<uint32_t>(height);

        vk::SurfaceCapabilitiesKHR caps = m_VkPhysicalDevice.get().getSurfaceCapabilitiesKHR(m_VkSurface.get());

        vk::SwapchainCreateInfoKHR swapchainInfo;
        swapchainInfo.surface = m_VkSurface.get();
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

        vk::SwapchainKHR swapchain = m_VkDevice.get().createSwapchainKHR(swapchainInfo);
        result.swapchain = vk::SharedSwapchainKHR(swapchain, m_VkDevice, m_VkSurface);

        std::vector<vk::Image> rawImages = m_VkDevice.get().getSwapchainImagesKHR(swapchain);

        for (const auto &img: rawImages) {
            vk::SharedImage sharedImg(img, m_VkDevice, vk::SwapchainOwns::yes);
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

            nvrhi::TextureHandle handle = m_NvrhiDevice->createHandleForNativeTexture(
                nvrhi::ObjectTypes::VK_Image,
                nvrhi::Object(img),
                textureDesc
            );
            result.backBuffers.push_back(handle);

            auto framebufferDesc = nvrhi::FramebufferDesc().addColorAttachment(handle);
            nvrhi::FramebufferHandle framebuffer = m_NvrhiDevice->createFramebuffer(framebufferDesc);
            result.framebuffers.push_back(framebuffer);
        }

        return result;
    }

    void ProcessEvents() {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                m_Running = false;
            }
            if (event.type == SDL_EVENT_WINDOW_RESIZED ||
                event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED ||
                event.type == SDL_EVENT_WINDOW_MINIMIZED ||
                event.type == SDL_EVENT_WINDOW_RESTORED) {
                m_NeedsResize = true;
            }
        }
    }

    void RenderFrame() {
        nvrhi::EventQueryHandle &currentRenderCompleteEvent = m_RenderCompleteEvents[m_CurrentFrame];

        // Wait for this frame's previous work to complete
        m_NvrhiDevice->waitEventQuery(currentRenderCompleteEvent);

        // Use per-frame acquire semaphore
        vk::SharedHandle<vk::Semaphore> &frameAcquireSemaphore = m_AcquireSemaphores[m_CurrentFrame];

        // Acquire next swapchain image
        uint32_t imageIndex;
        vk::Result res = m_VkDevice.get().acquireNextImageKHR(
            m_SwapchainData.swapchain.get(),
            UINT64_MAX,
            frameAcquireSemaphore.get(),
            nullptr,
            &imageIndex
        );

        if (res == vk::Result::eErrorOutOfDateKHR || res == vk::Result::eSuboptimalKHR) {
            m_NeedsResize = true;
            return;
        }

        if (res != vk::Result::eSuccess) return;

        // Use per-image render complete semaphore
        vk::SharedHandle<vk::Semaphore> &imageRenderCompleteSemaphore = m_RenderCompleteSemaphores[imageIndex];

        m_CommandList->open();

        const nvrhi::FramebufferHandle &currentFramebuffer = m_SwapchainData.framebuffers[imageIndex];
        nvrhi::Color purple(1.0f, 0.0f, 1.0f, 1.0f);
        nvrhi::utils::ClearColorAttachment(m_CommandList, currentFramebuffer, 0, purple);

        m_CommandList->close();

        m_NvrhiDevice->queueWaitForSemaphore(nvrhi::CommandQueue::Graphics, frameAcquireSemaphore.get(), 0);
        m_NvrhiDevice->queueSignalSemaphore(nvrhi::CommandQueue::Graphics, imageRenderCompleteSemaphore.get(), 0);

        m_NvrhiDevice->executeCommandList(m_CommandList);

        m_NvrhiDevice->resetEventQuery(currentRenderCompleteEvent);
        m_NvrhiDevice->setEventQuery(currentRenderCompleteEvent, nvrhi::CommandQueue::Graphics);

        // Present
        vk::SwapchainKHR rawSwapchain = m_SwapchainData.swapchain.get();
        vk::PresentInfoKHR presentInfo{};
        presentInfo.waitSemaphoreCount = 1;
        vk::Semaphore waitSemaphores[] = {imageRenderCompleteSemaphore.get()};
        presentInfo.pWaitSemaphores = waitSemaphores;
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &rawSwapchain;
        presentInfo.pImageIndices = &imageIndex;

        res = m_VkQueue.get().presentKHR(presentInfo);
        if (res == vk::Result::eErrorOutOfDateKHR || res == vk::Result::eSuboptimalKHR) {
            m_NeedsResize = true;
        }

        m_CurrentFrame = (m_CurrentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    }

private:
    // Member variables (order matters for destruction)
    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 3;

    // Window
    std::shared_ptr<SDL_Window> m_Window;

    // Vulkan objects
    vk::SharedInstance m_VkInstance;
    vk::DebugUtilsMessengerEXT m_DebugMessenger; // Debug messenger for validation
    vk::SharedPhysicalDevice m_VkPhysicalDevice;
    vk::SharedSurfaceKHR m_VkSurface;
    vk::SharedDevice m_VkDevice;
    vk::SharedQueue m_VkQueue;

    // NVRHI
    std::shared_ptr<NvrhiMessageCallback> m_MessageCallback;
    nvrhi::vulkan::DeviceHandle m_NvrhiDevice;
    nvrhi::CommandListHandle m_CommandList;

    // Swapchain
    SwapChainData m_SwapchainData;

    // Synchronization
    std::vector<vk::SharedHandle<vk::Semaphore>> m_AcquireSemaphores; // Per-frame (for acquire)
    std::vector<vk::SharedHandle<vk::Semaphore>> m_RenderCompleteSemaphores; // Per-swapchain-image
    std::array<nvrhi::EventQueryHandle, MAX_FRAMES_IN_FLIGHT> m_RenderCompleteEvents;
    uint32_t m_CurrentFrame = 0;

    // State
    bool m_Running = false;
    bool m_NeedsResize = false;
};


namespace
Engine {
    int Main(int argc, char **argv) {
        Application app;

        if (!app.Init()) {
            std::cerr << "Failed to initialize application" << std::endl;
            return -1;
        }

        app.Run();
        app.Destroy();

        return 0;
    }
}
