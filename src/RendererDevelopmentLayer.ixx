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
        static bool enabled = true;

        ImGui::Begin("TriangleBasedRenderingCommandList Profiling", &enabled);

        ImGui::Text("Frame rate: %.2f FPS", ImGui::GetIO().Framerate);

        mRenderer->BeginRendering();

        uint32_t texIdRed = mRenderer->RegisterVirtualTextureForThisFrame(mRedTextureHandle);
        uint32_t texIdGreen = mRenderer->RegisterVirtualTextureForThisFrame(mGreenTextureHandle);
        uint32_t texIdBlue = mRenderer->RegisterVirtualTextureForThisFrame(mBlueTextureHandle);

        // Draw a quad from (-200, -100) to (200, 100) with the red texture, but only show (-190, 90) to (190, 90)
        ClipRegion clipRect = ClipRegion::Quad(
            {
                {-190.f, -90.f},
                {-190.f, 90.f},
                {190.f, 90.f},
                {190.f, -90.f}
            },
            Engine::ClipMode::ShowOutside
        );

        mRenderer->DrawQuadTextureVirtual(
            {
                {-200.f, -100.f},
                {-200.f, 100.f},
                {200.f, 100.f},
                {200.f, -100.f}
            },
            {
                {0.f, 1.f},
                {0.f, 0.f},
                {1.f, 0.f},
                {1.f, 1.f}
            },
            texIdRed,
            std::nullopt,
            glm::u8vec4(255, 255, 255, 255),
            &clipRect
        );

        // Draw a clipped ring using the green texture
        ClipRegion clipRing = ClipRegion::Quad(
            {
                {-50.f, -50.f},
                {-50.f, 50.f},
                {50.f, 50.f},
                {50.f, -50.f}
            },
            Engine::ClipMode::ShowInside
        );

        mRenderer->DrawRing(
            {0.f,0.f},
            65.f,
            55.f,
            glm::u8vec4(127, 127, 255, 255),
            std::nullopt,
            &clipRing
            );

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
