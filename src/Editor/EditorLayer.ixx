export module Editor.EditorLayer;

import Core.Application;
import Core.Prelude;
import Vendor.ApplicationAPI;
import Editor.ImGuiRenderViewports;
import Render.Renderer2D;
import Core.Utilities;
import Render.FontResource;
import Render.FontRenderer;

namespace Editor {
    export class EditorLayer : public Engine::Layer {
    public:
        EditorLayer() = default;

        void OnAttach(const std::shared_ptr<Frosty::Application> &app) override {
            Layer::OnAttach(app);

            mSceneViewport.Init(mApp->GetNvrhiDevice());
            Engine::Renderer2DDescriptor desc{};
            desc.OutputSize = {1920, 1080};
            desc.VirtualSizeWidth = 500.f;
            mRenderer = std::make_shared<Engine::Renderer2D>(desc, mApp->GetNvrhiDevice());

            InitializeFontAsync();
        }

        virtual ~EditorLayer() override = default;

        void OnUpdate(std::chrono::duration<float> deltaTime) override;

        void OnDetach() override;

    private:
        bool mShowSceneViewport{true};

        ImGuiRenderViewport mSceneViewport;
        std::shared_ptr<Engine::Renderer2D> mRenderer;

        void InitializeFontAsync();

        Engine::Initializer mFontInitializer;
        std::shared_ptr<Engine::FontAtlasData> mFontData;
        nvrhi::TextureHandle mFontTexture;
    };
}
