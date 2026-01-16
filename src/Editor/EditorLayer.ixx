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
import Core.Utilities;

namespace Editor {
    export class EditorLayer : public Engine::Layer {
    public:
        EditorLayer() = default;

        void OnAttach(const Engine::Ref<Frosty::Application> &app) override;

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
