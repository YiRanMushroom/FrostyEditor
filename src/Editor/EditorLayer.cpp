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

import Render.Color;

import <glm/gtx/transform.hpp>;

namespace Editor {
    void EditorLayer::OnAttach(const Frosty::Ref<Frosty::Application> &app) {
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

        auto dockSpace = Engine::MakeRef<ComposableImGuiDockSpace>();

        // static_assert(std::same_as<decltype(WeakFromThis<EditorLayer>()), Frosty::Weak<EditorLayer>>);

        dockSpace->EmplaceContent([weak = WeakFromThis<EditorLayer>()] {
            if (auto self = weak.Lock()) {
                ImGui::BeginMainMenuBar();

                if (ImGui::BeginMenu("View")) {
                    ImGui::MenuItem("Scene Viewport", nullptr, &self->mShowSceneViewport);
                    ImGui::EndMenu();
                }

                ImGui::EndMainMenuBar();
            }
        });

        mDockSpace = dockSpace;
    }

    void EditorLayer::OnUpdate(std::chrono::duration<float> deltaTime) {
        Layer::OnUpdate(deltaTime);

        mDockSpace->RenderDockSpace();

        nvrhi::Color myBlueColor = Engine::Color::MyBlue;

        mRenderer->BeginRendering(
            {
                .ClearColor = myBlueColor
            }
        );

        if (mFontInitializer) {
            uint32_t virtualFontTextureID = mRenderer->RegisterVirtualTextureForThisFrame(mFontTexture);

            ImGui::Begin("Rotate angle (Y-axis)");
            ImGui::SliderFloat("Angle", &mRotationAngle, 0.0f, 360.0f);
            ImGui::End();

            Engine::DrawSimpleTextAsciiCommand drawTextCmd{};
            drawTextCmd
                    .SetColor(glm::u8vec4(255, 255, 255, 255))
                    .SetFontContext(mFontData.Get())
                    .SetVirtualFontTextureId(virtualFontTextureID)
                    .SetFontSize(128)
                    .SetStartPosition({-400.f, -200.f})
                    .SetEndPosition({400.f, 200.f})
                    .SetText("Hello from Frosty Editor!");

            // also set Transform, it is a mat4x4
            drawTextCmd.SetTransform(glm::rotate(glm::radians(mRotationAngle), glm::vec3(0.f, 1.f, 0.f)));

            mRenderer->Draw(drawTextCmd);
        }
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
        mRenderer.Reset();
        mFontData.Reset();
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

                const auto &executablePath = Engine::GetThisExecutablePath();

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

                Engine::GenerateFontAtlasInfo atlasInfo;
                atlasInfo.FontsToBake.push_back({
                    font.get(),
                    {msdf_atlas::Charset::ASCII}
                });

                mFontData = Engine::MakeRef<Engine::FontAtlasData>(GenerateFontAtlas(atlasInfo));

                Engine::GPUImageDescriptor imageDesc{};
                imageDesc.width = mFontData->AtlasWidth;
                imageDesc.height = mFontData->AtlasHeight;
                imageDesc.imageData = mFontData->GetAtlasBitmapDataSpan();
                imageDesc.debugName = "FontAtlasTexture";

                auto Device = mApp->GetNvrhiDevice();
                auto commandList = Device->createCommandList();

                mFontTexture = Engine::UploadImageToGPU(imageDesc, Device, commandList);
            }
        };
    }
}
