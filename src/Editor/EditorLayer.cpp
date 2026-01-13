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

        if (!mFontInitializer) {
            return;
        }

        auto virtualSize = mRenderer->BeginRendering();

        auto commandList = mRenderer->GetCommandList();

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
