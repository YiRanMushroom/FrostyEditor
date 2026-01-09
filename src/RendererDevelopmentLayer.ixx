export module RendererDevelopmentLayer;

import Core.Prelude;
import Core.Entrance;
import "SDL3/SDL.h";
import Core.Application;
import ImGui.ImGuiApplication;
import Render.Color;
import ImGui.ImGui;
import Render.Image;
import Core.STLExtension;
import Core.FileSystem;
import Core.Events;
import Vendor.ApplicationAPI;

import Render.GeneratedShaders;
import Render.FramebufferPresenter;
import Render.VirtualTextureManager;
import Render.Renderer2D;

import <cassert>;
import <cstddef>;

import glm;
import "glm/gtx/transform.hpp";

using namespace Engine;

export class RendererDevelopmentLayer : public Layer {
public:
    void OnAttach(const std::shared_ptr<Application> &app) override {
        Layer::OnAttach(app);
        auto &swapchain = mApp->GetSwapchainData();

        Renderer2DDescriptor rendererDesc;
        rendererDesc.Device = mApp->GetNvrhiDevice();
        rendererDesc.OutputSize = {swapchain.GetWidth(), swapchain.GetHeight()};
        rendererDesc.VirtualSize = {1920.0f, 1080.0f};

        mRenderer = std::make_shared<Renderer2D>(rendererDesc);

        mPresenter = std::make_shared<FramebufferPresenter>(mApp->GetNvrhiDevice().Get(),
                                                            swapchain.GetFramebufferInfo());

        auto &device = mApp->GetNvrhiDevice();
        auto &commandList = mApp->GetCommandList();

        commandList->open();

        mRedTextureHandle = CreateSolidColorTexture(
            device,
            commandList,
            nvrhi::Color(1.0f, 0.0f, 0.0f, 1.0f),
            "RedTexture"
        );

        mGreenTextureHandle = CreateSolidColorTexture(
            device,
            commandList,
            nvrhi::Color(0.0f, 1.0f, 0.0f, 1.0f),
            "GreenTexture"
        );

        mBlueTextureHandle = CreateSolidColorTexture(
            device,
            commandList,
            nvrhi::Color(0.0f, 0.0f, 1.0f, 1.0f),
            "BlueTexture"
        );

        commandList->close();
        device->executeCommandList(commandList);
    }

    virtual void OnRender(const nvrhi::CommandListHandle &commandList,
                          const nvrhi::FramebufferHandle &framebuffer, uint32_t frameIndex) override {
        ImGui::Begin("TriangleBasedRenderingCommandList Profiling");

        mRenderer->BeginRendering();

        const int quadCountHalfX = 150;
        const int quadCountHalfY = 100;

        const float quadSize = 5.0f;
        const float spacing = 0.0f;

        uint32_t texIdRed = mRenderer->RegisterVirtualTextureForThisFrame(mRedTextureHandle);
        uint32_t texIdGreen = mRenderer->RegisterVirtualTextureForThisFrame(mGreenTextureHandle);
        uint32_t texIdBlue = mRenderer->RegisterVirtualTextureForThisFrame(mBlueTextureHandle);

        // for (int y = -quadCountHalfY; y <= quadCountHalfY; ++y) {
        //     for (int x = -quadCountHalfX; x <= quadCountHalfX; ++x) {
        //         float posX = x * (quadSize + spacing);
        //         float posY = y * (quadSize + spacing);
        //
        //         glm::u8vec4 tintColor;
        //         uint32_t texId = texIdRed;
        //
        //         int modResult = ((x + y) % 3 + 3) % 3;
        //
        //         if (modResult == 0) {
        //             texId = texIdRed;
        //             tintColor = glm::u8vec4(255, 255, 255, 127);
        //         } else if (modResult == 1) {
        //             texId = texIdGreen;
        //             tintColor = glm::u8vec4(255, 255, 255, 127);
        //         } else {
        //             texId = texIdBlue;
        //             tintColor = glm::u8vec4(255, 128, 255, 127);
        //         }
        //         mRenderer->DrawQuadTextureVirtual(
        //             glm::mat4x2(
        //                 posX, posY,
        //                 posX + quadSize, posY,
        //                 posX + quadSize, posY + quadSize,
        //                 posX, posY + quadSize
        //             ),
        //             glm::mat4x2(
        //                 0.f, 0.f,
        //                 1.f, 0.f,
        //                 1.f, 1.f,
        //                 0.f, 1.f
        //             ),
        //             texId,
        //             std::nullopt,
        //             tintColor
        //         );
        //     }
        // }

        // Test line rendering (commented out for circle tests)
        // mRenderer->DrawLine(
        //     glm::vec2(-800.0f, -400.0f),
        //     glm::vec2(-400.0f, 0.0f),
        //     glm::u8vec4(255, 0, 0, 255)
        // );
        //
        // mRenderer->DrawLine(
        //     glm::vec2(-400.0f, -400.0f),
        //     glm::vec2(0.0f, 0.0f),
        //     glm::u8vec4(0, 255, 0, 255)
        // );
        //
        // mRenderer->DrawLine(
        //     glm::vec2(0.0f, -400.0f),
        //     glm::vec2(400.0f, 0.0f),
        //     glm::u8vec4(0, 0, 255, 255)
        // );
        //
        // mRenderer->DrawLine(
        //     glm::vec2(400.0f, -400.0f),
        //     glm::vec2(800.0f, 0.0f),
        //     glm::u8vec4(255, 255, 0, 255)
        // );
        //
        // mRenderer->DrawLine(
        //     glm::vec2(-800.0f, 0.0f),
        //     glm::vec2(-400.0f, 400.0f),
        //     glm::u8vec4(0, 255, 255, 255)
        // );
        //
        // mRenderer->DrawLine(
        //     glm::vec2(400.0f, 0.0f),
        //     glm::vec2(800.0f, 400.0f),
        //     glm::u8vec4(255, 0, 255, 255)
        // );

        // Test circle/ellipse rendering
        // Basic solid circles
        mRenderer->DrawCircle(glm::vec2(-400.0f, -200.0f), 80.0f, glm::u8vec4(255, 0, 0, 255));
        mRenderer->DrawCircle(glm::vec2(-200.0f, -200.0f), 60.0f, glm::u8vec4(0, 255, 0, 255));
        mRenderer->DrawCircle(glm::vec2(0.0f, -200.0f), 70.0f, glm::u8vec4(0, 0, 255, 255));
        mRenderer->DrawCircle(glm::vec2(200.0f, -200.0f), 50.0f, glm::u8vec4(255, 255, 0, 255));

        // Circles with transparency
        mRenderer->DrawCircle(glm::vec2(-300.0f, 0.0f), 90.0f, glm::u8vec4(255, 0, 255, 128));
        mRenderer->DrawCircle(glm::vec2(-100.0f, 0.0f), 85.0f, glm::u8vec4(0, 255, 255, 128));

        // Ellipses with rotation
        mRenderer->DrawEllipse(glm::vec2(150.0f, 0.0f), glm::vec2(100.0f, 50.0f), 0.0f,
                              glm::u8vec4(255, 128, 0, 255));
        mRenderer->DrawEllipse(glm::vec2(350.0f, 0.0f), glm::vec2(100.0f, 50.0f), 0.785f,
                              glm::u8vec4(128, 0, 255, 255));

        // Rings
        mRenderer->DrawRing(glm::vec2(-400.0f, 250.0f), 80.0f, 50.0f, glm::u8vec4(255, 0, 0, 255));
        mRenderer->DrawRing(glm::vec2(-200.0f, 250.0f), 70.0f, 50.0f, glm::u8vec4(0, 255, 0, 255));

        // Sectors (pie slices)
        const float PI = 3.14159265359f;
        mRenderer->DrawSector(glm::vec2(0.0f, 250.0f), 80.0f, 0.0f, PI * 0.5f,
                             glm::u8vec4(255, 255, 0, 255));
        mRenderer->DrawSector(glm::vec2(200.0f, 250.0f), 80.0f, PI * 0.25f, PI * 1.25f,
                             glm::u8vec4(0, 255, 255, 255));

        // Arcs (ring segments)
        mRenderer->DrawArc(glm::vec2(400.0f, 250.0f), 80.0f, 15.0f, 0.0f, PI * 1.5f,
                          glm::u8vec4(255, 0, 255, 255));

        mRenderer->EndRendering();

        // Present the renderer's output to the main framebuffer
        mPresenter->Present(commandList, mRenderer->GetTexture(), framebuffer);

        ImGui::End();
    }

    virtual bool OnEvent(const Event &event) override {
        if ((event.type == SDL_EVENT_WINDOW_RESIZED || event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)
            && event.window.windowID == SDL_GetWindowID(mApp->GetWindow().get())
        ) {
            int width = 0, height = 0;
            SDL_GetWindowSize(SDL_GetWindowFromID(event.window.windowID), &width, &height);
            mRenderer->OnResize(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
        }

        return false;
    }

private:
    std::shared_ptr<Renderer2D> mRenderer;
    std::shared_ptr<FramebufferPresenter> mPresenter;

    nvrhi::TextureHandle CreateSolidColorTexture(nvrhi::IDevice *device, nvrhi::ICommandList *cl, nvrhi::Color color,
                                                 const char *debugName) {
        nvrhi::TextureDesc desc;
        desc.width = 2;
        desc.height = 2;
        desc.format = nvrhi::Format::RGBA8_UNORM;
        desc.debugName = debugName;
        desc.isShaderResource = true;
        desc.initialState = nvrhi::ResourceStates::ShaderResource;
        desc.keepInitialState = true;

        auto tex = device->createTexture(desc);

        auto r = static_cast<uint8_t>(std::clamp(color.r * 255.f, 0.f, 255.f));
        auto g = static_cast<uint8_t>(std::clamp(color.g * 255.f, 0.f, 255.f));
        auto b = static_cast<uint8_t>(std::clamp(color.b * 255.f, 0.f, 255.f));
        auto a = static_cast<uint8_t>(std::clamp(color.a * 255.f, 0.f, 255.f));

        uint32_t pixel = (a << 24) | (b << 16) | (g << 8) | r;
        uint32_t data[4] = {pixel, pixel, pixel, pixel};

        cl->writeTexture(tex, 0, 0, data, 2 * sizeof(uint32_t));

        return tex;
    }

    nvrhi::TextureHandle mRedTextureHandle;
    nvrhi::TextureHandle mGreenTextureHandle;
    nvrhi::TextureHandle mBlueTextureHandle;
};
