module Editor.EditorLayer;

import Core.Application;
import Core.Prelude;
import Vendor.ApplicationAPI;
import Editor.ImGuiRenderViewports;
import Render.Renderer2D;
import Core.Utilities;
import Core.FileSystem;
import Render.FontResource;
import Render.Image;

namespace Editor {
    void EditorLayer::OnUpdate(std::chrono::duration<float> deltaTime) {
        Layer::OnUpdate(deltaTime);

        auto virtualSize = mRenderer->BeginRendering();

        mRenderer->DrawTriangleColored(
            glm::mat3x2(0.f, -100.f, -50.f, 0.f, 50.f, 0.f), // show face upward
            glm::u8vec4(255, 0, 0, 255)
        );

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

    void EditorLayer::OnDetach() {
        mFontInitializer.Reset();
        mSceneViewport = {};
        mRenderer.reset();
        mFontData.reset();
        mFontTexture.Reset();

        Layer::OnDetach();
    }

    void EditorLayer::InitializeFontAsync() {

    }
}
