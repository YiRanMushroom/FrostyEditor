#define VK_CHECK(res) if(res != VK_SUCCESS) { std::cerr << "Vulkan Error: " << res << std::endl; exit(-1); }

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

static NvrhiMessageCallback g_MessageCallback;

struct SwapChainData {
    vk::SharedSwapchainKHR swapchain;
    std::vector<vk::SharedImage> swapchainImages;
    std::vector<nvrhi::TextureHandle> backBuffers;
    std::vector<nvrhi::FramebufferHandle> framebuffers;
    uint32_t width;
    uint32_t height;
};

// Function to create or recreate swapchain
SwapChainData CreateSwapchain(
    SDL_Window *window,
    const vk::SharedDevice &device,
    const vk::SharedPhysicalDevice &physicalDevice,
    const vk::SharedSurfaceKHR &surface,
    const nvrhi::DeviceHandle &nvrhiDevice,
    vk::SwapchainKHR oldSwapchain = nullptr
) {
    SwapChainData result;

    // Get current window size
    int width, height;
    SDL_GetWindowSizeInPixels(window, &width, &height);
    result.width = static_cast<uint32_t>(width);
    result.height = static_cast<uint32_t>(height);

    // Get surface capabilities
    vk::SurfaceCapabilitiesKHR caps = physicalDevice.get().getSurfaceCapabilitiesKHR(surface.get());

    // Create swapchain
    vk::SwapchainCreateInfoKHR swapchainInfo;
    swapchainInfo.surface = surface.get();
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

    vk::SwapchainKHR swapchain = device.get().createSwapchainKHR(swapchainInfo);
    result.swapchain = vk::SharedSwapchainKHR(swapchain, device, surface);

    // Get swapchain images
    std::vector<vk::Image> rawImages = device.get().getSwapchainImagesKHR(swapchain);

    // Wrap images into SharedImage and NVRHI textures and create framebuffers
    for (const auto &img: rawImages) {
        // Create SharedImage with SwapChainOwns::yes so it won't be destroyed
        vk::SharedImage sharedImg(img, device, vk::SwapchainOwns::yes);
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

        nvrhi::TextureHandle handle = nvrhiDevice->createHandleForNativeTexture(
            nvrhi::ObjectTypes::VK_Image,
            nvrhi::Object(img),
            textureDesc
        );
        result.backBuffers.push_back(handle);

        auto framebufferDesc = nvrhi::FramebufferDesc()
                .addColorAttachment(handle);
        nvrhi::FramebufferHandle framebuffer = nvrhiDevice->createFramebuffer(framebufferDesc);
        result.framebuffers.push_back(framebuffer);
    }

    return result;
}

namespace
Engine {
    int Main(int argc, char **argv) {
        SDL_Window *window = SDL_CreateWindow("NVRHI Manual Swapchain", 1280, 720,
                                              SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
        if (!window) {
            std::cerr << "Failed to create window: " << SDL_GetError() << std::endl;
            return -1;
        }

        // -------------------------------------------------------------------------
        // 2. Initialize Vulkan Dispatcher Entry Point
        // -------------------------------------------------------------------------
        auto vkGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(SDL_Vulkan_GetVkGetInstanceProcAddr());
        if (!vkGetInstanceProcAddr) {
            std::cerr << "Failed to get Vulkan instance proc addr from SDL" << std::endl;
            return -1;
        }

        vk::detail::defaultDispatchLoaderDynamic.init(vkGetInstanceProcAddr);

        // -------------------------------------------------------------------------
        // 3. Initialize Vulkan Instance
        // -------------------------------------------------------------------------
        uint32_t extCount = 0;
        const char *const*extensions = SDL_Vulkan_GetInstanceExtensions(&extCount);
        std::vector<const char *> instanceExtensions(extensions, extensions + extCount);

        vk::ApplicationInfo appInfo;
        appInfo.apiVersion = vk::ApiVersion12;

        vk::InstanceCreateInfo instInfo;
        instInfo.pApplicationInfo = &appInfo;
        instInfo.enabledExtensionCount = static_cast<uint32_t>(instanceExtensions.size());
        instInfo.ppEnabledExtensionNames = instanceExtensions.data();

        vk::Instance instance = vk::createInstance(instInfo);
        vk::SharedInstance vkInstance(instance);
        vk::detail::defaultDispatchLoaderDynamic.init(vkInstance.get());

        // -------------------------------------------------------------------------
        // 4. Select Physical Device
        // -------------------------------------------------------------------------
        std::vector<vk::PhysicalDevice> physicalDevices = vkInstance.get().enumeratePhysicalDevices();
        if (physicalDevices.empty()) {
            std::cerr << "No Vulkan-capable GPU found" << std::endl;
            return -1;
        }
        vk::SharedPhysicalDevice vkPhysicalDevice(physicalDevices[0], vkInstance);

        // -------------------------------------------------------------------------
        // 5. Create Window Surface
        // -------------------------------------------------------------------------
        VkSurfaceKHR rawSurface;
        if (!SDL_Vulkan_CreateSurface(window, vkInstance.get(), nullptr, &rawSurface)) {
            std::cerr << "Failed to create Vulkan surface" << std::endl;
            return -1;
        }
        vk::SharedSurfaceKHR vkSurface(vk::SurfaceKHR(rawSurface), vkInstance);

        // -------------------------------------------------------------------------
        // 6. Create Logical Device
        // -------------------------------------------------------------------------
        float queuePriority = 1.0f;
        vk::DeviceQueueCreateInfo queueInfo;
        queueInfo.queueFamilyIndex = 0;
        queueInfo.queueCount = 1;
        queueInfo.pQueuePriorities = &queuePriority;

        const char *deviceExtensions[] = {
            vk::KHRSwapchainExtensionName
        };

        vk::PhysicalDeviceVulkan12Features features12;
        features12.descriptorIndexing = vk::True;
        features12.bufferDeviceAddress = vk::True;
        features12.runtimeDescriptorArray = vk::True;
        features12.shaderSampledImageArrayNonUniformIndexing = vk::True;
        features12.descriptorBindingPartiallyBound = vk::True;

        vk::DeviceCreateInfo devInfo;
        devInfo.pNext = &features12;
        devInfo.queueCreateInfoCount = 1;
        devInfo.pQueueCreateInfos = &queueInfo;
        devInfo.enabledExtensionCount = 1;
        devInfo.ppEnabledExtensionNames = deviceExtensions;

        vk::Device device = vkPhysicalDevice->createDevice(devInfo);

        // SharedDevice with default deleter
        vk::SharedDevice vkDevice(device);

        vk::detail::defaultDispatchLoaderDynamic.init(vkInstance.get(), vkDevice.get());

        vk::Queue queue = vkDevice.get().getQueue(0, 0);

        // SharedQueue - pass SharedDevice as parent
        vk::SharedQueue vkQueue(queue, vkDevice);

        // -------------------------------------------------------------------------
        // 7. Initialize NVRHI Device Wrapper
        // -------------------------------------------------------------------------
        nvrhi::vulkan::DeviceDesc nvrhiDesc;
        nvrhiDesc.errorCB = &g_MessageCallback;
        nvrhiDesc.instance = vkInstance.get();
        nvrhiDesc.physicalDevice = vkPhysicalDevice.get();
        nvrhiDesc.device = vkDevice.get();
        nvrhiDesc.graphicsQueue = vkQueue.get();
        nvrhiDesc.graphicsQueueIndex = 0;
        nvrhiDesc.deviceExtensions = deviceExtensions;
        nvrhiDesc.numDeviceExtensions = 1;

        nvrhi::vulkan::DeviceHandle nvrhiDevice = nvrhi::vulkan::createDevice(nvrhiDesc);
        nvrhi::DeviceHandle nvrhiValidationLayer;

        constexpr bool enableValidation = true;
        if (enableValidation) {
            nvrhiValidationLayer = nvrhi::validation::createValidationLayer(nvrhiDevice);
        }

        // -------------------------------------------------------------------------
        // 8. Create Swapchain and Synchronization (Triple Buffering)
        // -------------------------------------------------------------------------

        SwapChainData swapchainData = CreateSwapchain(window, vkDevice, vkPhysicalDevice, vkSurface, nvrhiDevice);

        // Triple buffering: create 3 sets of synchronization objects
        constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 3;

        // Semaphores for each frame - signaled when swapchain image is acquired
        std::array<vk::SharedHandle<vk::Semaphore>, MAX_FRAMES_IN_FLIGHT> acquireSemaphores;

        // Semaphores for each frame - signaled when rendering is complete
        std::array<vk::SharedHandle<vk::Semaphore>, MAX_FRAMES_IN_FLIGHT> renderCompleteSemaphores;

        // EventQueries for CPU-GPU synchronization (used for swapchain rebuild)
        std::array<nvrhi::EventQueryHandle, MAX_FRAMES_IN_FLIGHT> renderCompleteEvents;

        vk::SemaphoreCreateInfo semaphoreInfo;
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
            vk::Semaphore acquireSem = vkDevice.get().createSemaphore(semaphoreInfo);
            acquireSemaphores[i] = vk::SharedHandle<vk::Semaphore>(acquireSem, vkDevice);

            vk::Semaphore renderSem = vkDevice.get().createSemaphore(semaphoreInfo);
            renderCompleteSemaphores[i] = vk::SharedHandle<vk::Semaphore>(renderSem, vkDevice);

            renderCompleteEvents[i] = nvrhiDevice->createEventQuery();
        }

        nvrhi::CommandListHandle commandList = nvrhiDevice->createCommandList();

        uint32_t currentFrame = 0;

        // -------------------------------------------------------------------------
        // 9. Main Loop
        // -------------------------------------------------------------------------
        bool running = true;
        bool needsResize = false;

        while (running) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_EVENT_QUIT) {
                    running = false;
                }
                if (event.type == SDL_EVENT_WINDOW_RESIZED || event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED ||
                    event.type == SDL_EVENT_WINDOW_MINIMIZED || event.type == SDL_EVENT_WINDOW_RESTORED) {
                    needsResize = true;
                }
            }

            // Handle swapchain recreation if window was resized
            if (needsResize) {
                // Wait for all in-flight frames to complete before recreating swapchain
                for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
                    nvrhiDevice->waitEventQuery(renderCompleteEvents[i]);
                }

                // Store old swapchain for recreation
                vk::SwapchainKHR oldSwapchain = swapchainData.swapchain ? swapchainData.swapchain.get() : nullptr;

                // Recreate swapchain
                swapchainData = CreateSwapchain(window, vkDevice, vkPhysicalDevice, vkSurface, nvrhiDevice,
                                                oldSwapchain);

                needsResize = false;
                currentFrame = 0; // Reset frame counter after swapchain rebuild
                continue;
            }

            // Get synchronization objects for current frame
            vk::SharedHandle<vk::Semaphore> &currentAcquireSemaphore = acquireSemaphores[currentFrame];
            vk::SharedHandle<vk::Semaphore> &currentRenderCompleteSemaphore = renderCompleteSemaphores[currentFrame];
            nvrhi::EventQueryHandle &currentRenderCompleteEvent = renderCompleteEvents[currentFrame];

            // Wait for this frame's previous work to complete (ensures we don't overwrite in-use resources)
            nvrhiDevice->waitEventQuery(currentRenderCompleteEvent);

            // Acquire next swapchain image
            uint32_t imageIndex;
            vk::Result res = vkDevice.get().acquireNextImageKHR(
                swapchainData.swapchain.get(),
                UINT64_MAX,
                currentAcquireSemaphore.get(), // Signal when image is ready
                nullptr,
                &imageIndex
            );

            if (res == vk::Result::eErrorOutOfDateKHR || res == vk::Result::eSuboptimalKHR) {
                needsResize = true;
                continue;
            }

            if (res != vk::Result::eSuccess) continue;

            commandList->open();

            // Get the current framebuffer for this swapchain image
            const nvrhi::FramebufferHandle &currentFramebuffer = swapchainData.framebuffers[imageIndex];

            // Clear the primary render target using NVRHI utility function
            nvrhi::Color purple(1.0f, 0.0f, 1.0f, 1.0f);
            nvrhi::utils::ClearColorAttachment(commandList, currentFramebuffer, 0, purple);

            commandList->close();

            nvrhiDevice->queueSignalSemaphore(nvrhi::CommandQueue::Graphics, currentAcquireSemaphore.get(), 0);
            nvrhiDevice->executeCommandList(commandList);

            // Reset and set EventQuery to track when this frame's rendering completes
            nvrhiDevice->resetEventQuery(currentRenderCompleteEvent);
            nvrhiDevice->setEventQuery(currentRenderCompleteEvent, nvrhi::CommandQueue::Graphics);

            // Present the rendered image
            // Wait on renderCompleteSemaphore to ensure rendering is done before present
            const vk::SwapchainKHR rawSwapchain = swapchainData.swapchain.get();
            vk::PresentInfoKHR presentInfo{};
            presentInfo.waitSemaphoreCount = 1;
            vk::Semaphore waitSemaphores[] = {currentRenderCompleteSemaphore.get()};
            presentInfo.pWaitSemaphores = waitSemaphores;
            presentInfo.swapchainCount = 1;
            presentInfo.pSwapchains = &rawSwapchain;
            presentInfo.pImageIndices = &imageIndex;

            res = vkQueue.get().presentKHR(presentInfo);
            if (res == vk::Result::eErrorOutOfDateKHR || res == vk::Result::eSuboptimalKHR) {
                needsResize = true;
            }

            // Move to next frame
            currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
        }

        // -------------------------------------------------------------------------
        // 10. Cleanup
        // -------------------------------------------------------------------------
        vkDevice->waitIdle();

        SDL_DestroyWindow(window);

        return 0;
    }
}
