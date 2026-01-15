export module Editor.EditorLayer;

import Core.Application;
import Core.Prelude;
import Vendor.ApplicationAPI;
import Editor.ImGuiRenderViewports;
import Render.Renderer2D;
import Core.Utilities;
import Render.FontResource;
import Render.TextRenderer;
import Render.Transform;

namespace Editor {
    export class EditorLayer : public Engine::Layer {
    public:
        EditorLayer() = default;

        void OnAttach(const std::shared_ptr<Frosty::Application> &app) override {
            Layer::OnAttach(app);

            mSceneViewport.Init(mApp->GetNvrhiDevice());
            Engine::Renderer2DDescriptor desc{};
            desc.OutputSize = {1920, 1080};
            Engine::Ref<Engine::VirtualSizeTransform> virtualSizeTransform =
                Engine::Ref<Engine::VirtualSizeTransform>::Create();
            virtualSizeTransform->SetVirtualWidth(1920.f);
            desc.Transforms = std::vector<Engine::Ref<Engine::ITransform>>{
                virtualSizeTransform
            };
            mRenderer = Engine::MakeRef<Engine::Renderer2D>(desc, mApp->GetNvrhiDevice());

            InitializeFontAsync();
        }

        virtual ~EditorLayer() override = default;

        void OnUpdate(std::chrono::duration<float> deltaTime) override;

        void OnDetach() override;

    private:
        bool mShowSceneViewport{true};

        ImGuiRenderViewport mSceneViewport;
        Engine::Ref<Engine::Renderer2D> mRenderer;

        void InitializeFontAsync();

        Engine::Initializer mFontInitializer;
        std::shared_ptr<Engine::FontAtlasData> mFontData;
        nvrhi::TextureHandle mFontTexture;
    };
}
