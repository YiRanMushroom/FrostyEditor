#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <nvrhi/vulkan.h>
#include <nvrhi/utils.h>
#include <nvrhi/validation.h>
#include <vector>
#include <iostream>
#include <windows.h>

#define VK_CHECK(res) if(res != VK_SUCCESS) { std::cerr << "Vulkan Error: " << res << std::endl; exit(-1); }

import Core.Prelude;
import Core.Entrance;

// Simple message callback for NVRHI
class NvrhiMessageCallback : public nvrhi::IMessageCallback {
public:
    void message(nvrhi::MessageSeverity severity, const char* messageText) override {
        const char* severityStr = "";
        switch (severity) {
            case nvrhi::MessageSeverity::Info: severityStr = "INFO"; break;
            case nvrhi::MessageSeverity::Warning: severityStr = "WARNING"; break;
            case nvrhi::MessageSeverity::Error: severityStr = "ERROR"; break;
            case nvrhi::MessageSeverity::Fatal: severityStr = "FATAL"; break;
        }
        std::cerr << "[NVRHI " << severityStr << "] " << messageText << std::endl;
    }
};

static NvrhiMessageCallback g_MessageCallback;

namespace Engine {
    int Main(int argc, char **argv) {
        // -------------------------------------------------------------------------
        // 1. Initialize SDL3
        // -------------------------------------------------------------------------
        if (SDL_Init(SDL_INIT_VIDEO) < 0) {
            std::cerr << "Failed to initialize SDL: " << SDL_GetError() << std::endl;
            return -1;
        }

        SDL_Window *window = SDL_CreateWindow("NVRHI Manual Swapchain", 1280, 720,
                                              SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
        if (!window) {
            std::cerr << "Failed to create window: " << SDL_GetError() << std::endl;
            return -1;
        }

        // -------------------------------------------------------------------------
        // 2. Initialize Vulkan Dispatcher Entry Point
        // -------------------------------------------------------------------------

        // Use SDL3 official method to get vkGetInstanceProcAddr
        auto vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)SDL_Vulkan_GetVkGetInstanceProcAddr();
        if (!vkGetInstanceProcAddr) {
            std::cerr << "Failed to get Vulkan instance proc addr from SDL" << std::endl;
            return -1;
        }

        // Initialize the global dispatcher with the base function pointer
        vk::detail::defaultDispatchLoaderDynamic.init(vkGetInstanceProcAddr);

        // -------------------------------------------------------------------------
        // 3. Initialize Vulkan Instance (using vulkan.hpp RAII)
        // -------------------------------------------------------------------------
        uint32_t extCount = 0;
        const char *const*extensions = SDL_Vulkan_GetInstanceExtensions(&extCount);
        std::vector<const char*> instanceExtensions(extensions, extensions + extCount);

        vk::ApplicationInfo appInfo;
        appInfo.apiVersion = VK_API_VERSION_1_2;

        vk::InstanceCreateInfo instInfo;
        instInfo.pApplicationInfo = &appInfo;
        instInfo.enabledExtensionCount = static_cast<uint32_t>(instanceExtensions.size());
        instInfo.ppEnabledExtensionNames = instanceExtensions.data();

        // Create RAII context and instance
        vk::raii::Context vkContext;
        vk::raii::Instance vkInstance(vkContext, instInfo);

        // Update the dispatcher with the instance to load instance-level functions
        vk::detail::defaultDispatchLoaderDynamic.init(*vkInstance);

        // -------------------------------------------------------------------------
        // 4. Select Physical Device (using vulkan.hpp RAII)
        // -------------------------------------------------------------------------
        std::vector<vk::raii::PhysicalDevice> physicalDevices = vkInstance.enumeratePhysicalDevices();
        if (physicalDevices.empty()) {
            std::cerr << "No Vulkan-capable GPU found" << std::endl;
            return -1;
        }
        vk::raii::PhysicalDevice vkPhysicalDevice = std::move(physicalDevices[0]);

        // -------------------------------------------------------------------------
        // 5. Create Window Surface (using vulkan.hpp RAII)
        // -------------------------------------------------------------------------
        VkSurfaceKHR rawSurface;
        if (!SDL_Vulkan_CreateSurface(window, *vkInstance, nullptr, &rawSurface)) {
            std::cerr << "Failed to create Vulkan surface" << std::endl;
            return -1;
        }
        // Wrap the raw surface in RAII handle
        vk::raii::SurfaceKHR vkSurface(vkInstance, rawSurface);

        // -------------------------------------------------------------------------
        // 6. Create Logical Device (using vulkan.hpp RAII)
        // -------------------------------------------------------------------------
        float queuePriority = 1.0f;
        vk::DeviceQueueCreateInfo queueInfo;
        queueInfo.queueFamilyIndex = 0;
        queueInfo.queueCount = 1;
        queueInfo.pQueuePriorities = &queuePriority;

        const char *deviceExtensions[] = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME
        };

        vk::PhysicalDeviceVulkan12Features features12;
        features12.descriptorIndexing = VK_TRUE;
        features12.bufferDeviceAddress = VK_TRUE;
        features12.runtimeDescriptorArray = VK_TRUE;
        features12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
        features12.descriptorBindingPartiallyBound = VK_TRUE;

        vk::DeviceCreateInfo devInfo;
        devInfo.pNext = &features12;
        devInfo.queueCreateInfoCount = 1;
        devInfo.pQueueCreateInfos = &queueInfo;
        devInfo.enabledExtensionCount = 1;
        devInfo.ppEnabledExtensionNames = deviceExtensions;

        vk::raii::Device vkDevice(vkPhysicalDevice, devInfo);

        // FINAL STEP: Initialize the dispatcher with the logical device
        // This loads device-level functions like vkCreateSemaphore
        vk::detail::defaultDispatchLoaderDynamic.init(*vkInstance, *vkDevice);

        vk::raii::Queue vkQueue(vkDevice, 0, 0);

        // -------------------------------------------------------------------------
        // 7. Initialize NVRHI Device Wrapper
        // -------------------------------------------------------------------------
        nvrhi::vulkan::DeviceDesc nvrhiDesc;
        nvrhiDesc.errorCB = &g_MessageCallback;
        nvrhiDesc.instance = *vkInstance;           // Dereference RAII handle
        nvrhiDesc.physicalDevice = *vkPhysicalDevice; // Dereference RAII handle
        nvrhiDesc.device = *vkDevice;               // Dereference RAII handle
        nvrhiDesc.graphicsQueue = *vkQueue;         // Dereference RAII handle
        nvrhiDesc.graphicsQueueIndex = 0;
        nvrhiDesc.deviceExtensions = deviceExtensions;
        nvrhiDesc.numDeviceExtensions = 1;

        nvrhi::DeviceHandle nvrhiDevice = nvrhi::vulkan::createDevice(nvrhiDesc);

        // Optional: Wrap with validation layer for debugging
        constexpr bool enableValidation = true;
        if (enableValidation) {
            nvrhi::DeviceHandle nvrhiValidationLayer = nvrhi::validation::createValidationLayer(nvrhiDevice);
            nvrhiDevice = nvrhiValidationLayer;
        }

        // -------------------------------------------------------------------------
        // 8. Manual Vulkan Swapchain Creation (using vulkan.hpp RAII)
        // -------------------------------------------------------------------------
        int width, height;
        SDL_GetWindowSizeInPixels(window, &width, &height);

        vk::SurfaceCapabilitiesKHR caps = vkPhysicalDevice.getSurfaceCapabilitiesKHR(*vkSurface);

        vk::SwapchainCreateInfoKHR swapchainInfo;
        swapchainInfo.surface = *vkSurface;
        swapchainInfo.minImageCount = 2;
        swapchainInfo.imageFormat = vk::Format::eB8G8R8A8Unorm;
        swapchainInfo.imageColorSpace = vk::ColorSpaceKHR::eSrgbNonlinear;
        swapchainInfo.imageExtent = vk::Extent2D(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
        swapchainInfo.imageArrayLayers = 1;
        swapchainInfo.imageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferDst;
        swapchainInfo.preTransform = caps.currentTransform;
        swapchainInfo.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
        swapchainInfo.presentMode = vk::PresentModeKHR::eFifo;
        swapchainInfo.clipped = VK_TRUE;
        swapchainInfo.oldSwapchain = nullptr;

        vk::raii::SwapchainKHR vkSwapchain(vkDevice, swapchainInfo);

        std::vector<vk::Image> swapchainImages = vkSwapchain.getImages();

        // -------------------------------------------------------------------------
        // 9. Wrap Native Images into NVRHI Texture Handles and Create Framebuffers
        // -------------------------------------------------------------------------
        std::vector<nvrhi::TextureHandle> nvrhiBackBuffers;
        std::vector<nvrhi::FramebufferHandle> nvrhiFramebuffers;

        for (const auto& img: swapchainImages) {
            // Use the builder pattern as shown in the tutorial
            auto textureDesc = nvrhi::TextureDesc()
                .setDimension(nvrhi::TextureDimension::Texture2D)
                .setFormat(nvrhi::Format::BGRA8_UNORM)
                .setWidth(width)
                .setHeight(height)
                .setIsRenderTarget(true)
                .setDebugName("BackBuffer")
                .setInitialState(nvrhi::ResourceStates::Present)
                .setKeepInitialState(true);

            nvrhi::TextureHandle handle = nvrhiDevice->createHandleForNativeTexture(
                nvrhi::ObjectTypes::VK_Image,
                nvrhi::Object(static_cast<VkImage>(img)),  // Cast vk::Image to VkImage
                textureDesc
            );
            nvrhiBackBuffers.push_back(handle);

            // Create a framebuffer for this swapchain image (as per tutorial)
            auto framebufferDesc = nvrhi::FramebufferDesc()
                .addColorAttachment(handle);
            nvrhi::FramebufferHandle framebuffer = nvrhiDevice->createFramebuffer(framebufferDesc);
            nvrhiFramebuffers.push_back(framebuffer);
        }

        vk::SemaphoreCreateInfo semaphoreInfo;
        vk::raii::Semaphore acquireSemaphore(vkDevice, semaphoreInfo);

        nvrhi::CommandListHandle commandList = nvrhiDevice->createCommandList();

        // -------------------------------------------------------------------------
        // 10. Main Loop
        // -------------------------------------------------------------------------
        bool running = true;
        while (running) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_EVENT_QUIT) running = false;
            }

            uint32_t imageIndex;
            VkResult res = vkAcquireNextImageKHR(*vkDevice, *vkSwapchain, UINT64_MAX, *acquireSemaphore, VK_NULL_HANDLE,
                                                 &imageIndex);
            if (res != VK_SUCCESS) continue;

            commandList->open();

            // Get the current framebuffer for this swapchain image
            const nvrhi::FramebufferHandle& currentFramebuffer = nvrhiFramebuffers[imageIndex];

            // Clear the primary render target using NVRHI utility function (as per tutorial)
            nvrhi::Color purple(1.0f, 0.0f, 1.0f, 1.0f);
            nvrhi::utils::ClearColorAttachment(commandList, currentFramebuffer, 0, purple);

            commandList->close();
            nvrhiDevice->executeCommandList(commandList);

            vkQueueWaitIdle(*vkQueue);

            VkSwapchainKHR rawSwapchain = *vkSwapchain;
            VkPresentInfoKHR presentInfo = {VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
            presentInfo.swapchainCount = 1;
            presentInfo.pSwapchains = &rawSwapchain;
            presentInfo.pImageIndices = &imageIndex;

            vkQueuePresentKHR(*vkQueue, &presentInfo);
        }

        // -------------------------------------------------------------------------
        // 11. Cleanup
        // -------------------------------------------------------------------------
        vkDeviceWaitIdle(*vkDevice);

        // Clear NVRHI resources first (before destroying Vulkan objects)
        nvrhiFramebuffers.clear();
        nvrhiBackBuffers.clear();
        commandList = nullptr;
        nvrhiDevice = nullptr;

        // Note: All RAII handles (vkSwapchain, vkSurface, vkDevice, vkInstance, etc.)
        // will be automatically destroyed when they go out of scope
        // No manual vkDestroy* calls needed!

        SDL_DestroyWindow(window);
        SDL_Quit();

        return 0;
    }
}