module Editor.EditorLayer;

import Core.Application;
import Core.Prelude;
import Vendor.ApplicationAPI;
import Editor.ImGuiRenderViewports;
import Render.Renderer2D;
import Core.Utilities;
import Core.FileSystem;
import FontResource;
import Render.Image;

namespace Editor {
    void EditorLayer::OnUpdate(std::chrono::duration<float> deltaTime) {
        Layer::OnUpdate(deltaTime);

        if (!mFontInitializer) {
            return;
        }

        auto virtualSize = mRenderer->BeginRendering();

        auto commandList = mRenderer->GetCommandList();

        uint32_t fontTextureID = mRenderer->RegisterVirtualTextureForThisFrame(mFontTexture);

        // render the font atlas in black from -100, -100 to 100, 100
        mRenderer->DrawQuadFontColoredVirtual({
            {-100.f, 100.f}, {100.f, 100.f},
            {100.f, -100.f}, {-100.f, -100.f}
        },{
            {0.f, 0.f}, {1.f, 0.f},
            {1.f, 1.f}, {0.f, 1.f}
        }, fontTextureID, {0, 0, 0, 255}, mFontData->MSDFPixelRange, std::nullopt);

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
        mFontInitializer = {
            [this] {
                std::unique_ptr<msdfgen::FreetypeHandle, decltype([](msdfgen::FreetypeHandle *ptr) {
                        if (ptr) {
                            msdfgen::deinitializeFreetype(ptr);
                        }
                    }
                )> ftLib(msdfgen::initializeFreetype());

                const auto &executablePath = Engine::GetExecutablePath();

                // load font from fonts/JetBrainsMono-Regular.ttf

                auto fontPath = executablePath / "fonts" / "JetBrainsMono-Regular.ttf";
                std::unique_ptr<msdfgen::FontHandle, decltype([](msdfgen::FontHandle *ptr) {
                        if (ptr) {
                            msdfgen::destroyFont(ptr);
                        }
                    }
                )> font(nullptr);

                // Initialize
                if (auto *fontHandle = msdfgen::loadFont(ftLib.get(), fontPath.string().c_str())) {
                    font.reset(fontHandle);
                } else {
                    throw std::runtime_error("Failed to load font from path: " + fontPath.string());
                }

                GenerateFontAtlasInfo atlasInfo;
                atlasInfo.FontsToBake.push_back({
                    font.get(),
                    {msdf_atlas::Charset::ASCII}
                });

                mFontData = GenerateFontAtlas(atlasInfo);

                Engine::SimpleGPUImageDescriptor imageDesc{};
                imageDesc.width = mFontData->AtlasWidth;
                imageDesc.height = mFontData->AtlasHeight;
                imageDesc.imageData = std::span(
                    reinterpret_cast<const uint32_t*>(mFontData->AtlasBitmapData.get()), mFontData->PixelCount);
                imageDesc.debugName = "FontAtlasTexture";

                auto Device = mApp->GetNvrhiDevice();
                auto commandList = Device->createCommandList();

                mFontTexture = Engine::UploadImageToGPU(imageDesc, Device, commandList);
            }
        };
    }
}
