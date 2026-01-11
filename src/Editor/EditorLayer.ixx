export module Editor.EditorLayer;

import Core.Application;
import Core.Prelude;
import Vendor.ApplicationAPI;
import Editor.ImGuiRenderViewports;
import Render.Renderer2D;

namespace Editor {
    export class EditorLayer : public Engine::Layer {
    public:
        EditorLayer() = default;

        void OnAttach(const std::shared_ptr<Frosty::Application> &app) override {
            Layer::OnAttach(app);

            mSceneViewport.Init(mApp->GetNvrhiDevice());
            Engine::Renderer2DDescriptor desc{};
            desc.Device = mApp->GetNvrhiDevice();
            desc.OutputSize = {1920, 1080};
            desc.VirtualSizeWidth = 1000.f;
            mRenderer = std::make_shared<Engine::Renderer2D>(desc);
        }

        virtual ~EditorLayer() override = default;

        void OnUpdate(std::chrono::duration<float> deltaTime) override {
            Layer::OnUpdate(deltaTime);

            auto virtualSize = mRenderer->BeginRendering();

            auto commandList = mRenderer->GetCommandList();
            commandList->clearTextureFloat(
                mRenderer->GetTexture(),
                nvrhi::AllSubresources,
                nvrhi::Color(0.5f, 0.0f, 0.5f, 1.0f)
            );

            Engine::ClipRegion region = Engine::ClipRegion::Quad(
                {
                    {-25.f, 25.f}, {25.f, 25.f},
                    {25.f, -25.f}, {-25.f, -25.f}
                }, Engine::ClipMode::ShowOutside);

            mRenderer->DrawCircle({0.f, 0.f}, 100.f, {255, 0, 255, 255}, std::nullopt, &region);

            mRenderer->EndRendering();

            mSceneViewport.ShowViewport(&mShowSceneViewport, "Scene Viewport");

            if (mSceneViewport.NeedsResize()) {
                auto size = mSceneViewport.GetExpectedViewportSize();
                if (size.x > 0.0f && size.y > 0.0f) {
                    mRenderer->OnResize(static_cast<uint32_t>(size.x), static_cast<uint32_t>(size.y));
                    mSceneViewport.SetViewportTexture(mRenderer->GetTexture());
                }
            }
        }

    private:
        bool mShowSceneViewport{true};

        ImGuiRenderViewport mSceneViewport;
        std::shared_ptr<Engine::Renderer2D> mRenderer;
    };
}
