#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.hpp>
#include <nvrhi/vulkan.h>
#include <nvrhi/utils.h>
#include <vector>
#include <iostream>
#include <windows.h>

#define VK_CHECK(res) if(res != VK_SUCCESS) { std::cerr << "Vulkan Error: " << res << std::endl; exit(-1); }

// Define the global dispatcher storage required by vulkan.hpp and NVRHI

import Core.Prelude;
import Core.Entrance;

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
        // 3. Initialize Vulkan Instance
        // -------------------------------------------------------------------------
        uint32_t extCount = 0;
        const char *const*extensions = SDL_Vulkan_GetInstanceExtensions(&extCount);
        std::vector<const char*> instanceExtensions(extensions, extensions + extCount);

        VkApplicationInfo appInfo = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
        appInfo.apiVersion = VK_API_VERSION_1_2;

        VkInstanceCreateInfo instInfo = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        instInfo.pApplicationInfo = &appInfo;
        instInfo.enabledExtensionCount = (uint32_t) instanceExtensions.size();
        instInfo.ppEnabledExtensionNames = instanceExtensions.data();

        VkInstance vkInstance;
        VK_CHECK(vkCreateInstance(&instInfo, nullptr, &vkInstance));

        // Update the dispatcher with the instance to load instance-level functions
        vk::detail::defaultDispatchLoaderDynamic.init(vk::Instance(vkInstance));

        // -------------------------------------------------------------------------
        // 4. Select Physical Device
        // -------------------------------------------------------------------------
        uint32_t gpuCount = 0;
        vkEnumeratePhysicalDevices(vkInstance, &gpuCount, nullptr);
        std::vector<VkPhysicalDevice> gpus(gpuCount);
        vkEnumeratePhysicalDevices(vkInstance, &gpuCount, gpus.data());
        VkPhysicalDevice vkPhysicalDevice = gpus[0];

        // -------------------------------------------------------------------------
        // 5. Create Window Surface
        // -------------------------------------------------------------------------
        VkSurfaceKHR vkSurface;
        if (!SDL_Vulkan_CreateSurface(window, vkInstance, nullptr, &vkSurface)) {
            std::cerr << "Failed to create Vulkan surface" << std::endl;
            return -1;
        }

        // -------------------------------------------------------------------------
        // 6. Create Logical Device
        // -------------------------------------------------------------------------
        float queuePriority = 1.0f;
        VkDeviceQueueCreateInfo queueInfo = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        queueInfo.queueFamilyIndex = 0;
        queueInfo.queueCount = 1;
        queueInfo.pQueuePriorities = &queuePriority;

        const char *deviceExtensions[] = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME
        };

        VkPhysicalDeviceVulkan12Features features12 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
        features12.descriptorIndexing = VK_TRUE;
        features12.bufferDeviceAddress = VK_TRUE;
        features12.runtimeDescriptorArray = VK_TRUE;
        features12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
        features12.descriptorBindingPartiallyBound = VK_TRUE;

        VkDeviceCreateInfo devInfo = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        devInfo.pNext = &features12;
        devInfo.queueCreateInfoCount = 1;
        devInfo.pQueueCreateInfos = &queueInfo;
        devInfo.enabledExtensionCount = 1;
        devInfo.ppEnabledExtensionNames = deviceExtensions;

        VkDevice vkDevice;
        VK_CHECK(vkCreateDevice(vkPhysicalDevice, &devInfo, nullptr, &vkDevice));

        // FINAL STEP: Initialize the dispatcher with the logical device
        // This loads device-level functions like vkCreateSemaphore
        vk::detail::defaultDispatchLoaderDynamic.init(vk::Instance(vkInstance), vk::Device(vkDevice));

        VkQueue vkQueue;
        vkGetDeviceQueue(vkDevice, 0, 0, &vkQueue);

        // -------------------------------------------------------------------------
        // 7. Initialize NVRHI Device Wrapper
        // -------------------------------------------------------------------------
        nvrhi::vulkan::DeviceDesc nvrhiDesc;
        nvrhiDesc.instance = vkInstance;
        nvrhiDesc.physicalDevice = vkPhysicalDevice;
        nvrhiDesc.device = vkDevice;
        nvrhiDesc.graphicsQueue = vkQueue;
        nvrhiDesc.graphicsQueueIndex = 0;
        nvrhiDesc.deviceExtensions = deviceExtensions;
        nvrhiDesc.numDeviceExtensions = 1;

        nvrhi::DeviceHandle nvrhiDevice = nvrhi::vulkan::createDevice(nvrhiDesc);

        // -------------------------------------------------------------------------
        // 8. Manual Vulkan Swapchain Creation
        // -------------------------------------------------------------------------
        int width, height;
        SDL_GetWindowSizeInPixels(window, &width, &height);

        VkSurfaceCapabilitiesKHR caps;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vkPhysicalDevice, vkSurface, &caps);

        VkSwapchainCreateInfoKHR swapchainInfo = {VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
        swapchainInfo.surface = vkSurface;
        swapchainInfo.minImageCount = 2;
        swapchainInfo.imageFormat = VK_FORMAT_B8G8R8A8_UNORM;
        swapchainInfo.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        swapchainInfo.imageExtent = {(uint32_t) width, (uint32_t) height};
        swapchainInfo.imageArrayLayers = 1;
        swapchainInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        swapchainInfo.preTransform = caps.currentTransform;
        swapchainInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        swapchainInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
        swapchainInfo.clipped = VK_TRUE;
        swapchainInfo.oldSwapchain = VK_NULL_HANDLE;

        VkSwapchainKHR vkSwapchain;
        VK_CHECK(vkCreateSwapchainKHR(vkDevice, &swapchainInfo, nullptr, &vkSwapchain));

        uint32_t imageCount;
        vkGetSwapchainImagesKHR(vkDevice, vkSwapchain, &imageCount, nullptr);
        std::vector<VkImage> swapchainImages(imageCount);
        vkGetSwapchainImagesKHR(vkDevice, vkSwapchain, &imageCount, swapchainImages.data());

        // -------------------------------------------------------------------------
        // 9. Wrap Native Images into NVRHI Texture Handles
        // -------------------------------------------------------------------------
        std::vector<nvrhi::TextureHandle> nvrhiBackBuffers;
        for (auto img: swapchainImages) {
            nvrhi::TextureDesc textureDesc;
            textureDesc.dimension = nvrhi::TextureDimension::Texture2D;
            textureDesc.format = nvrhi::Format::SBGRA8_UNORM;
            textureDesc.width = width;
            textureDesc.height = height;
            textureDesc.isRenderTarget = true;
            textureDesc.debugName = "BackBuffer";
            textureDesc.initialState = nvrhi::ResourceStates::Present;
            textureDesc.keepInitialState = true;

            nvrhi::TextureHandle handle = nvrhiDevice->createHandleForNativeTexture(
                nvrhi::ObjectTypes::VK_Image,
                nvrhi::Object(img),
                textureDesc
            );
            nvrhiBackBuffers.push_back(handle);
        }

        VkSemaphore acquireSemaphore;
        VkSemaphoreCreateInfo semaphoreInfo = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VK_CHECK(vkCreateSemaphore(vkDevice, &semaphoreInfo, nullptr, &acquireSemaphore));

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
            VkResult res = vkAcquireNextImageKHR(vkDevice, vkSwapchain, UINT64_MAX, acquireSemaphore, VK_NULL_HANDLE,
                                                 &imageIndex);
            if (res != VK_SUCCESS) continue;

            commandList->open();

            nvrhi::TextureHandle currentBackBuffer = nvrhiBackBuffers[imageIndex];

            nvrhi::Color purple = {1.0f, 0.0f, 1.0f, 1.0f};
            auto desc = currentBackBuffer->getDesc();
            commandList->clearTextureFloat(currentBackBuffer,
                                           nvrhi::TextureSubresourceSet(0, desc.mipLevels, 0, desc.arraySize), purple);

            commandList->close();
            nvrhiDevice->executeCommandList(commandList);

            vkQueueWaitIdle(vkQueue);

            VkPresentInfoKHR presentInfo = {VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
            presentInfo.swapchainCount = 1;
            presentInfo.pSwapchains = &vkSwapchain;
            presentInfo.pImageIndices = &imageIndex;

            vkQueuePresentKHR(vkQueue, &presentInfo);
        }

        // -------------------------------------------------------------------------
        // 11. Cleanup
        // -------------------------------------------------------------------------
        vkDeviceWaitIdle(vkDevice);

        nvrhiBackBuffers.clear();
        commandList = nullptr;
        nvrhiDevice = nullptr;

        vkDestroySemaphore(vkDevice, acquireSemaphore, nullptr);
        vkDestroySwapchainKHR(vkDevice, vkSwapchain, nullptr);
        vkDestroySurfaceKHR(vkInstance, vkSurface, nullptr);
        vkDestroyDevice(vkDevice, nullptr);
        vkDestroyInstance(vkInstance, nullptr);

        SDL_DestroyWindow(window);
        SDL_Quit();

        return 0;
    }
}