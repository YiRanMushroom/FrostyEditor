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
import "glm/gtx/transform.hpp";

namespace Editor {
    export class PerspectiveCamera : public Engine::ITransform, public Engine::RefCounted {
    public:
        void OnFramebufferResized(float newWidth, float newHeight) override;

        void DoTransform(glm::mat4 &matrix) override;

        void OnUpdate(std::chrono::duration<float> deltaTime);

        bool OnEvent(const Engine::Event &event);

    private:
        float mFOV = 90.f;
        float mNearPlane = 0.1f;
        float mFarPlane = 1000.f;
        glm::vec3 mPosition{0.f, 0.f, 100.f};
        glm::vec3 mTarget{0.f, 0.f, 0.f};
        glm::vec3 mUp{0.f, 1.f, 0.f};

        glm::mat4 mProjectionMatrix{};
        glm::mat4 mViewMatrix{};

        bool mMatricesDirty = true;

        float mAspectRatio{};
    };

    void PerspectiveCamera::OnFramebufferResized(float newWidth, float newHeight) {
        mAspectRatio = newWidth / newHeight;
        mMatricesDirty = true;
    }

    void PerspectiveCamera::OnUpdate(std::chrono::duration<float> deltaTime) {
        const float cameraSpeed = 5.0f; // units per second
        glm::vec3 forward = glm::normalize(mTarget - mPosition);
        glm::vec3 right = glm::normalize(glm::cross(forward, mUp));

        if (Engine::IsKeyPressed(SDL_SCANCODE_W)) {
            mPosition += forward * cameraSpeed * deltaTime.count();
            mTarget += forward * cameraSpeed * deltaTime.count();
            mMatricesDirty = true;
        }
        if (Engine::IsKeyPressed(SDL_SCANCODE_S)) {
            mPosition -= forward * cameraSpeed * deltaTime.count();
            mTarget -= forward * cameraSpeed * deltaTime.count();
            mMatricesDirty = true;
        }
        if (Engine::IsKeyPressed(SDL_SCANCODE_A)) {
            mPosition -= right * cameraSpeed * deltaTime.count();
            mTarget -= right * cameraSpeed * deltaTime.count();
            mMatricesDirty = true;
        }
        if (Engine::IsKeyPressed(SDL_SCANCODE_D)) {
            mPosition += right * cameraSpeed * deltaTime.count();
            mTarget += right * cameraSpeed * deltaTime.count();
            mMatricesDirty = true;
        }
    }

    bool PerspectiveCamera::OnEvent(const Engine::Event &event) {
        if (event.type == SDL_EVENT_MOUSE_WHEEL) {
            const auto &wheelEvent = event.wheel;
            float fovChange = -static_cast<float>(wheelEvent.y) * 5.0f; // Zoom speed
            mFOV += fovChange;
            mFOV = glm::clamp(mFOV, 30.0f, 120.0f); // Clamp FOV between 30 and 120 degrees
            mMatricesDirty = true;
            return true;
        }

        if (event.type == SDL_EVENT_MOUSE_MOTION) {
            const auto &motionEvent = event.motion;
            if (Engine::IsMouseButtonPressed(SDL_BUTTON_RIGHT)) {
                float sensitivity = 0.1f; // Mouse sensitivity
                float yaw = motionEvent.xrel * sensitivity;
                float pitch = motionEvent.yrel * sensitivity;

                glm::vec3 direction = mTarget - mPosition;
                glm::vec3 right = glm::normalize(glm::cross(direction, mUp));
                glm::vec3 up = glm::normalize(glm::cross(right, direction));

                // Rotate around the up vector (yaw)
                glm::mat4 yawRotation = glm::rotate(glm::radians(-yaw), mUp);
                direction = glm::vec3(yawRotation * glm::vec4(direction, 0.0f));

                // Rotate around the right vector (pitch)
                glm::mat4 pitchRotation = glm::rotate(glm::radians(-pitch), right);
                direction = glm::vec3(pitchRotation * glm::vec4(direction, 0.0f));

                mTarget = mPosition + direction;
                mMatricesDirty = true;
                return true;
            }
        }

        return false;
    }

    void PerspectiveCamera::DoTransform(glm::mat4 &matrix) {
        if (mMatricesDirty) {
            mProjectionMatrix = glm::perspective(
                glm::radians(mFOV),
                mAspectRatio,
                mNearPlane,
                mFarPlane
            );

            mViewMatrix = glm::lookAt(
                mPosition,
                mTarget,
                mUp
            );

            mMatricesDirty = false;
        }

        matrix = mProjectionMatrix * mViewMatrix * matrix;
    }
}

namespace Editor {
    export class EditorLayer : public Engine::Layer {
    public:
        EditorLayer() = default;

        void OnAttach(const Engine::Ref<Frosty::Application> &app) override;

        virtual ~EditorLayer() override = default;

        void OnUpdate(std::chrono::duration<float> deltaTime) override;

        void OnDetach() override;

        bool OnEvent(const Frosty::Event &event) override;

    private:
        bool mShowSceneViewport{true};

        ImGuiRenderViewport mSceneViewport;

        Engine::Ref<PerspectiveCamera> mCamera;

        Engine::Ref<Engine::Renderer2D> mRenderer;

        void InitializeFontAsync();

        Engine::Initializer mFontInitializer;
        Engine::Ref<Engine::FontAtlasData> mFontData;
        nvrhi::TextureHandle mFontTexture;
        Engine::Ref<ImGuiDockSpace> mDockSpace;

        float mRotationAngle{0.0f};

        bool mFocusedOnViewport{false};
    };
}
