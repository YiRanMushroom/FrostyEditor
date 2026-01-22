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

import "SDL3/SDL_keycode.h";
import Vendor.ImGuizmo;
import "SDL3/SDL_mouse.h";

namespace Editor {
    class SimpleTransform : public Engine::ITransform, public Engine::RefCounted {
    public:
        SimpleTransform() = default;

        glm::mat4 &GetMatrix() {
            return mMatrix;
        }

        const glm::mat4 &GetMatrix() const {
            return mMatrix;
        }

        void SetMatrix(const glm::mat4 &matrix) {
            mMatrix = matrix;
        }

        void OnFramebufferResized(float newWidth, float newHeight) {
            // No action needed for this simple transform
        }

        void DoTransform(glm::mat4 &matrix) override {
            matrix = mMatrix * matrix;
        }

    private:
        glm::mat4 mMatrix{1.0f};
    };

    void EditorLayer::OnAttach(const Frosty::Ref<Frosty::Application> &app) {
        Layer::OnAttach(app);

        mSceneViewport.Init(mApp->GetNvrhiDevice());
        Engine::Renderer2DDescriptor desc{};
        desc.OutputSize = {1920, 1080};
        // Engine::Ref<Engine::VirtualSizeTransform> virtualSizeTransform =
        //         Engine::Ref<Engine::VirtualSizeTransform>::Create();
        // virtualSizeTransform->SetVirtualWidth(1920.f);
        // desc.Transforms = std::vector<Engine::Ref<Engine::ITransform>>{
        //     virtualSizeTransform
        // };
        mCamera = Engine::MakeRef<PerspectiveCamera>();
        desc.Transforms = std::vector{
            mCamera.As<Engine::ITransform>()
        };
        mRenderer = Engine::MakeRef<Engine::Renderer2D>(desc, mApp->GetCommandListSubmissionContext());

        InitializeFontAsync();

        auto dockSpace = Engine::MakeRef<ComposableImGuiDockSpace>();

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

        // Register entity transforms
        // Entity ID 1 is the triangle
        mEntityTransforms.insert({1, Engine::MakeRef<SimpleTransform>()});
    }

    void EditorLayer::OnUpdate(std::chrono::duration<float> deltaTime) {
        Layer::OnUpdate(deltaTime);

        // Only update camera if viewport is focused AND ImGuizmo is not being used
        // Check ImGuizmo state directly here (this is from previous frame's rendering)
        if (mFocusedOnViewport && !ImGuizmo::IsUsing() && !ImGuizmo::IsOver())
            mCamera->OnUpdate(deltaTime);

        mDockSpace->RenderDockSpace();

        nvrhi::Color myBlueColor = Engine::Color::MyBlue;

        mRenderer->BeginRendering(
            {
                .ClearColor = myBlueColor
            }
        );

        Engine::TriangleDrawCommand triangleCmd{};
        triangleCmd
                .SetPositions(
                    glm::vec2(0.f, -100.f),
                    glm::vec2(-50.f, 0.f),
                    glm::vec2(50.f, 0.f)
                )
                .SetTintColor({255, 0, 0, 255})
                .SetEntityID(1);

        // Apply transform if entity has one
        if (mEntityTransforms.contains(1)) {
            auto transform = static_cast<SimpleTransform *>(mEntityTransforms[1].Get());
            if (transform) {
                triangleCmd.SetTransform(transform->GetMatrix());
            }
        }

        mRenderer->Draw(triangleCmd);

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
                    .SetText("Hello from Frosty Editor!")
                    .SetEntityID(0);

            // also set Transform, it is a mat4x4
            drawTextCmd.SetTransform(glm::rotate(glm::radians(mRotationAngle), glm::vec3(0.f, 1.f, 0.f)));

            mRenderer->Draw(drawTextCmd);
        }

        mRenderer->EndRendering();

        mFocusedOnViewport = mSceneViewport.ShowViewport(&mShowSceneViewport, "Scene Viewport", [this] {
            RenderImGuizmoInViewport();
        });

        mLastClickedTextureOffset = mSceneViewport.GetLastClickedTextureOffset();

        if (mSceneViewport.NeedsResize()) {
            auto size = mSceneViewport.GetExpectedViewportSize();
            if (size.x > 0.0f && size.y > 0.0f) {
                mRenderer->OnResize(static_cast<uint32_t>(size.x), static_cast<uint32_t>(size.y));
                mSceneViewport.SetViewportTexture(mRenderer->GetTexture());
            }
        }

        RenderImGuizmo(deltaTime);
    }

    void EditorLayer::OnDetach() {
        mFontInitializer.Reset();
        mSceneViewport = {};
        mRenderer.Reset();
        mFontData.Reset();
        mFontTexture.Reset();

        Layer::OnDetach();
    }

    bool EditorLayer::OnEvent(const Engine::Event &event) {
        // Handle mouse button down events (initial click only)
        if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT) {
            // This is the initial click - check SHIFT state NOW
            bool isShiftClick = Engine::GetKeyModifiers() & SDL_KMOD_SHIFT;
            // bool isClickInViewport = mSceneViewport.IsWindowHovered();

            // Debug: Print state
            std::cout << std::format("BUTTON_DOWN: IsOver={},  isShift={}\n",
                                     ImGuizmo::IsOver(), isShiftClick);

            // SHIFT+click for selection
            if (isShiftClick) {
                // Don't do entity picking if clicking on ImGuizmo
                return HandleMouseSelect(event);
            }


            // Normal click (no SHIFT) - check if should deselect
            // Only check ImGuizmo::IsOver() when we have an active transform (gizmo is visible)
            // This prevents IsOver() from returning stale state when no gizmo is displayed
            if (mActiveTransform && !ImGuizmo::IsOver()) {
                std::cout << "Deselecting active transform\n";
                mActiveTransform.Reset();
            }

            return Layer::OnEvent(event);
        }

        // For motion and wheel events
        // Check if ImGuizmo is active - if so, don't pass to camera
        if (ImGuizmo::IsUsing() || ImGuizmo::IsOver()) {
            return Layer::OnEvent(event);
        }

        // Only pass events to camera if viewport is focused
        if (mFocusedOnViewport) {
            return Layer::OnEvent(event) || mCamera->OnEvent(event);
        }

        return Layer::OnEvent(event);
    }

    bool EditorLayer::HandleMouseSelect(const Engine::Event &event) {
        if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
            // This function is only called when SHIFT+clicking in viewport, not on ImGuizmo
            std::cout << std::format("Entity picking at texture offset: ({}, {})\n",
                                     mLastClickedTextureOffset.x,
                                     mLastClickedTextureOffset.y);

            uint32_t entityID = mRenderer->GetEntityIDAtPixelPositionAsync(
                glm::uvec2(
                    static_cast<uint32_t>(mLastClickedTextureOffset.x),
                    static_cast<uint32_t>(mLastClickedTextureOffset.y)
                )
            ).IntoFuture().get();

            if (entityID != 0 && mEntityTransforms.contains(entityID)) {
                mActiveTransform = mEntityTransforms[entityID];
                std::cout << "Selected entity ID: " << entityID << std::endl;
            } else if (!ImGuizmo::IsOver()) {
                mActiveTransform.Reset();
                if (entityID == 0) {
                    std::cout << "No entity at clicked position." << std::endl;
                } else {
                    std::cout << "Entity ID " << entityID << " has no associated transform." << std::endl;
                }
            } else {
                std::cout << "Clicked on ImGuizmo, not changing selection." << std::endl;
            }

            return true;
        }


        return false;
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

                // auto Device = mApp->GetNvrhiDevice();

                mFontTexture = Engine::UploadImageToGPU(imageDesc, mApp->GetCommandListSubmissionContext());
            }
        };
    }

    void EditorLayer::RenderImGuizmo(std::chrono::duration<float> deltaTime) {
        if (!mActiveTransform) {
            return;
        }

        // Keyboard shortcuts for switching gizmo operation
        // Only when viewport is focused and no text input is being edited
        if (mFocusedOnViewport && !ImGui::GetIO().WantTextInput) {
            if (ImGui::IsKeyPressed(ImGuiKey_R)) {
                // Cycle through operation modes: Translate -> Rotate -> Scale -> Translate
                if (mCurrentGizmoOperation == ImGuizmo::TRANSLATE) {
                    mCurrentGizmoOperation = ImGuizmo::ROTATE;
                } else if (mCurrentGizmoOperation == ImGuizmo::ROTATE) {
                    mCurrentGizmoOperation = ImGuizmo::SCALE;
                } else {
                    mCurrentGizmoOperation = ImGuizmo::TRANSLATE;
                }
            }
            if (ImGui::IsKeyPressed(ImGuiKey_S)) {
                mUseSnap = !mUseSnap;
            }
        }

        // Show gizmo control window
        ImGui::Begin("Transform Controls");

        if (ImGui::RadioButton("Translate", mCurrentGizmoOperation == ImGuizmo::TRANSLATE)) {
            mCurrentGizmoOperation = ImGuizmo::TRANSLATE;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Rotate", mCurrentGizmoOperation == ImGuizmo::ROTATE)) {
            mCurrentGizmoOperation = ImGuizmo::ROTATE;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Scale", mCurrentGizmoOperation == ImGuizmo::SCALE)) {
            mCurrentGizmoOperation = ImGuizmo::SCALE;
        }

        // Get the active transform matrix
        auto simpleTransform = static_cast<SimpleTransform *>(mActiveTransform.Get());
        if (!simpleTransform) {
            ImGui::End();
            return;
        }

        glm::mat4 &matrix = simpleTransform->GetMatrix();

        // Decompose matrix for manual editing
        float matrixTranslation[3], matrixRotation[3], matrixScale[3];
        ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(matrix), matrixTranslation, matrixRotation, matrixScale);

        ImGui::InputFloat3("Translation", matrixTranslation);
        ImGui::InputFloat3("Rotation", matrixRotation);
        ImGui::InputFloat3("Scale", matrixScale);

        // Only recompose if values changed
        glm::mat4 newMatrix;
        ImGuizmo::RecomposeMatrixFromComponents(matrixTranslation, matrixRotation, matrixScale,
                                                glm::value_ptr(newMatrix));
        if (newMatrix != matrix) {
            matrix = newMatrix;
        }

        // Mode selection (not available for scale)
        if (mCurrentGizmoOperation != ImGuizmo::SCALE) {
            if (ImGui::RadioButton("Local", mCurrentGizmoMode == ImGuizmo::LOCAL)) {
                mCurrentGizmoMode = ImGuizmo::LOCAL;
            }
            ImGui::SameLine();
            if (ImGui::RadioButton("World", mCurrentGizmoMode == ImGuizmo::WORLD)) {
                mCurrentGizmoMode = ImGuizmo::WORLD;
            }
        }

        // Snap settings
        ImGui::Checkbox("Use Snap", &mUseSnap);
        ImGui::SameLine();

        switch (mCurrentGizmoOperation) {
            case ImGuizmo::TRANSLATE:
                ImGui::InputFloat3("Snap", mSnapTranslation);
                break;
            case ImGuizmo::ROTATE:
                ImGui::InputFloat("Angle Snap", &mSnapRotation);
                break;
            case ImGuizmo::SCALE:
                ImGui::InputFloat("Scale Snap", &mSnapScale);
                break;
            default:
                break;
        }

        ImGui::Text("Shortcuts: T=Translate, R=Rotate, E=Scale, S=Toggle Snap");

        ImGui::End();
    }

    void EditorLayer::RenderImGuizmoInViewport() {
        if (!mActiveTransform || !mCamera) {
            return;
        }

        auto simpleTransform = static_cast<SimpleTransform *>(mActiveTransform.Get());
        if (!simpleTransform) {
            return;
        }

        // Get camera matrices
        glm::mat4 view = mCamera->mViewMatrix;
        glm::mat4 projection = mCamera->mProjectionMatrix;

        // Flip Y-axis for ImGuizmo to match Vulkan coordinate system
        glm::mat4 projectionForImGuizmo = projection;
        projectionForImGuizmo[1][1] *= -1.0f;

        // Get the object matrix
        glm::mat4 &matrix = simpleTransform->GetMatrix();

        // Store previous matrix to detect changes
        mPreviousTransform = matrix;

        // Set ImGuizmo to render on top of the viewport image
        ImGuizmo::SetDrawlist();

        // Get the viewport's screen position and size
        // The viewport texture was rendered, so we need to get its position
        ImVec2 viewportPos = mSceneViewport.GetCursorPosition();
        ImVec2 viewportSize = mSceneViewport.GetExpectedViewportSize();

        ImGuizmo::SetRect(viewportPos.x, viewportPos.y, viewportSize.x, viewportSize.y);

        // Determine snap value based on operation
        float *snapValues = nullptr;
        float snapRotationArray[3] = {mSnapRotation, mSnapRotation, mSnapRotation};
        float snapScaleArray[3] = {mSnapScale, mSnapScale, mSnapScale};

        if (mUseSnap) {
            switch (mCurrentGizmoOperation) {
                case ImGuizmo::TRANSLATE:
                    snapValues = mSnapTranslation;
                    break;
                case ImGuizmo::ROTATE:
                    snapValues = snapRotationArray;
                    break;
                case ImGuizmo::SCALE:
                    snapValues = snapScaleArray;
                    break;
                default:
                    break;
            }
        }

        // Manipulate the gizmo
        ImGuizmo::Manipulate(
            glm::value_ptr(view),
            glm::value_ptr(projectionForImGuizmo),
            mCurrentGizmoOperation,
            mCurrentGizmoMode,
            glm::value_ptr(matrix),
            nullptr,
            snapValues
        );

        // Only update if the transform actually changed (to avoid accumulating errors from decompose/recompose)
        mTransformChanged = (matrix != mPreviousTransform);
    }
}
