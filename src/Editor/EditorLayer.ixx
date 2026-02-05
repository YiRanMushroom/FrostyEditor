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

import Core.Input;
import Core.Events;

import Render.Transform;
import "SDL3/SDL.h";
import Vendor.ImGuizmo;

namespace Editor {
    export class PerspectiveCamera : public Engine::ITransform, public Engine::RefCounted {
    public:
        void OnFramebufferResized(float newWidth, float newHeight) override {
            mAspectRatio = newWidth / newHeight;
            mMatricesDirty = true;
        }

        void OnUpdate(std::chrono::duration<float> deltaTime) {
            const float moveSpeed = 500.0f;
            float dt = deltaTime.count();

            glm::vec3 forward = glm::normalize(mFocalPoint - mPosition);
            glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0, 1, 0)));
            glm::vec3 up = glm::cross(right, forward);

            bool moved = false;
            if (Engine::IsKeyPressed(SDL_SCANCODE_W)) {
                mFocalPoint += forward * moveSpeed * dt;
                moved = true;
            }
            if (Engine::IsKeyPressed(SDL_SCANCODE_S)) {
                mFocalPoint -= forward * moveSpeed * dt;
                moved = true;
            }
            if (Engine::IsKeyPressed(SDL_SCANCODE_A)) {
                mFocalPoint -= right * moveSpeed * dt;
                moved = true;
            }
            if (Engine::IsKeyPressed(SDL_SCANCODE_D)) {
                mFocalPoint += right * moveSpeed * dt;
                moved = true;
            }

            if (moved) mMatricesDirty = true;
        }

        bool OnEvent(const Engine::Event &event) {
            if (event.type == SDL_EVENT_MOUSE_WHEEL) {
                float delta = event.wheel.y;
                mDistance -= delta * (mDistance * 0.1f);
                mDistance = std::max(0.1f, mDistance);
                UpdatePositionFromOrbit();
                mMatricesDirty = true;
                return true;
            }

            if (event.type == SDL_EVENT_MOUSE_MOTION) {
                float dx = event.motion.xrel * 0.2f;
                float dy = event.motion.yrel * 0.2f;

                // Check if SHIFT key is pressed - if so, don't process camera movement
                // SHIFT is reserved for selection operations
                bool shiftPressed = Engine::GetKeyModifiers() & SDL_KMOD_SHIFT;

                if (Engine::IsMouseButtonPressed(SDL_BUTTON_MIDDLE)) {
                    float panSpeed = mDistance * 0.0015f;
                    glm::vec3 offset = GetRightVector() * (-dx * panSpeed) + GetUpVector() * (-dy * panSpeed);
                    mPosition += offset;
                    mFocalPoint += offset;
                    mMatricesDirty = true;
                } else if (Engine::IsMouseButtonPressed(SDL_BUTTON_RIGHT)) {
                    mYaw -= dx;
                    mPitch -= dy;
                    mPitch = std::clamp(mPitch, -89.0f, 89.0f);
                    UpdatePositionFromOrbit();
                    mMatricesDirty = true;
                } else if (Engine::IsMouseButtonPressed(SDL_BUTTON_LEFT) && !shiftPressed) {
                    // Only process left button if SHIFT is NOT pressed
                    mYaw -= dx;
                    mPitch -= dy;
                    mPitch = std::clamp(mPitch, -89.0f, 89.0f);
                    UpdateFocalPointFromSelfRotate();
                    mMatricesDirty = true;
                }
                return true;
            }
            return false;
        }

        void DoTransform(glm::mat4 &matrix) override {
            if (mMatricesDirty) {
                UpdateCameraMatrices();
                mMatricesDirty = false;
            }
            matrix = mProjectionMatrix * mViewMatrix * matrix;
        }

        glm::mat4 mProjectionMatrix{1.f};
        glm::mat4 mViewMatrix{1.f};

    private:
        void UpdateCameraMatrices() {
            float x = mDistance * cos(glm::radians(mPitch)) * sin(glm::radians(mYaw));
            float y = mDistance * sin(glm::radians(mPitch));
            float z = mDistance * cos(glm::radians(mPitch)) * cos(glm::radians(mYaw));

            mPosition = mFocalPoint + glm::vec3(x, y, z);

            mViewMatrix = glm::lookAt(mPosition, mFocalPoint, glm::vec3(0, 1, 0));

            mProjectionMatrix = glm::perspective(
                glm::radians(mFOV),
                mAspectRatio,
                mNearPlane,
                mFarPlane
            );
        }

        glm::vec3 GetDirectionFromAngles() {
            float x = cos(glm::radians(mPitch)) * sin(glm::radians(mYaw));
            float y = sin(glm::radians(mPitch));
            float z = cos(glm::radians(mPitch)) * cos(glm::radians(mYaw));
            return glm::vec3(x, y, z);
        }

        void UpdatePositionFromOrbit() {
            mPosition = mFocalPoint + GetDirectionFromAngles() * mDistance;
        }

        void UpdateFocalPointFromSelfRotate() {
            mFocalPoint = mPosition - GetDirectionFromAngles() * mDistance;
        }

        glm::vec3 GetForwardVector() { return glm::normalize(mFocalPoint - mPosition); }
        glm::vec3 GetRightVector() { return glm::normalize(glm::cross(GetForwardVector(), glm::vec3(0, 1, 0))); }
        glm::vec3 GetUpVector() { return glm::cross(GetRightVector(), GetForwardVector()); }

        float mFOV = 60.f;
        float mNearPlane = 0.1f;
        float mFarPlane = 5000.f;
        float mAspectRatio = 1.77f;

        glm::vec3 mFocalPoint{0.f, 0.f, 0.f};
        float mDistance = 500.f;
        float mYaw = 0.f;
        float mPitch = 20.f;

        glm::vec3 mPosition{};
        bool mMatricesDirty = true;
    };
}

namespace Editor {
    export class EditorLayer : public Engine::Layer {
    public:
        EditorLayer() : Layer{}, m_AsyncInitContext(this) {}

        void OnAttach(const Engine::Ref<Frosty::Application> &app) override;

        virtual ~EditorLayer() override;

        void OnUpdate(std::chrono::duration<float> deltaTime) override;

        void OnDetach() override;

        bool OnEvent(const Engine::Event &event) override;

        bool HandleMouseSelect(const Engine::Event &event);


    private:
        bool mShowSceneViewport{true};

        ImGuiRenderViewport mSceneViewport;

        Engine::Ref<PerspectiveCamera> mCamera;

        Engine::Ref<Engine::NVRenderer2D> mRenderer;

        void InitializeFontAsync();

        Engine::AsyncInitializationContext m_AsyncInitContext;

        Engine::Ref<Engine::FontAtlasData> mFontData;
        nvrhi::TextureHandle mFontTexture;
        Engine::Ref<ImGuiDockSpace> mDockSpace;

        float mRotationAngle{0.0f};

        bool mFocusedOnViewport{false};

        ImVec2 mLastClickedTextureOffset{0.0f, 0.0f};

        Engine::Ref<Engine::ITransform> mActiveTransform;

        std::unordered_map<uint32_t, Engine::Ref<Engine::ITransform>> mEntityTransforms;

        // ImGuizmo state
        ImGuizmo::OPERATION mCurrentGizmoOperation{ImGuizmo::TRANSLATE};
        ImGuizmo::MODE mCurrentGizmoMode{ImGuizmo::WORLD};
        bool mUseSnap{false};
        float mSnapTranslation[3] = {1.f, 1.f, 1.f};
        float mSnapRotation = 15.f;
        float mSnapScale = 0.1f;

        bool mTransformResetRequested{false};

        // For detecting if transform actually changed to avoid accumulating errors
        glm::mat4 mPreviousTransform{1.0f};
        bool mTransformChanged{false};

        void RenderImGuizmo(std::chrono::duration<float> deltaTime);

        void RenderImGuizmoInViewport();
    };
}
