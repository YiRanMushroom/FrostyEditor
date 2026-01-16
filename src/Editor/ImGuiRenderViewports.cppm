export module Editor.ImGuiRenderViewports;

import Core.Prelude;
import Vendor.ApplicationAPI;
import ImGui.ImGui;
import Core.Utilities;

namespace Editor {
    export class ImGuiRenderViewport {
    public:
        ImGuiRenderViewport() = default;

        void Init(nvrhi::DeviceHandle device) {
            mDevice = std::move(device);
        }

        bool NeedsResize() const {
            return !mViewportTexture || mViewportTexture.GetTextureDesc().width != mExpectedSize.x ||
                   mViewportTexture.GetTextureDesc().height != mExpectedSize.y;
        }

        void ShowViewport(bool *open, const char *title = "ImGui Viewport") {
            if (*open) {
                ImGui::SetNextWindowSizeConstraints({150.f, 150.f},
                                                    {
                                                        std::numeric_limits<float>::max(),
                                                        std::numeric_limits<float>::max()
                                                    });

                ImGui::Begin(title, open,
                             ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoScrollWithMouse);

                mExpectedSize = ImGui::GetContentRegionAvail();

                if (mViewportTexture) {
                    ImGui::ImageAutoManaged(mViewportTexture, ImGui::GetContentRegionAvail());
                }

                ImGui::End();
            }
        }

        [[nodiscard]] const ImVec2 &GetExpectedViewportSize() const {
            return mExpectedSize;
        }

        void SetViewportTexture(nvrhi::TextureHandle texture) {
            mViewportTexture = ImGui::ImGuiImage::Create(
                std::move(texture),
                mDevice->createSampler(nvrhi::SamplerDesc{}.setAllFilters(true))
            );
        }

    private:
        ImVec2 mExpectedSize{0.0f, 0.0f};
        nvrhi::DeviceHandle mDevice;
        ImGui::ImGuiImage mViewportTexture;
    };

    export class ImGuiDockSpace : public Engine::RefCounted {
    public:
        virtual void RenderDockSpace() {
            // We are using the ImGuiWindowFlags_NoDocking flag to make the parent window not dockable into,
            // because it would be confusing to have two docking targets within each others.
            ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDocking;

            const ImGuiViewport *viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(viewport->WorkPos);
            ImGui::SetNextWindowSize(viewport->WorkSize);
            ImGui::SetNextWindowViewport(viewport->ID);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse
                    | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
            window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

            // When using ImGuiDockNodeFlags_PassthruCentralNode, DockSpace() will render our background
            // and handle the pass-thru hole, so we ask Begin() to not render a background.
            // if (dockspace_flags & ImGuiDockNodeFlags_PassthruCentralNode)
            //     window_flags |= ImGuiWindowFlags_NoBackground;

            // Important: note that we proceed even if Begin() returns false (aka window is collapsed).
            // This is because we want to keep our DockSpace() active. If a DockSpace() is inactive,
            // all active windows docked into it will lose their parent and become undocked.
            // We cannot preserve the docking relationship between an active window and an inactive docking, otherwise
            // any change of dockspace/settings would lead to windows being stuck in limbo and never being visible.

            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
            ModifyWindowFlags(window_flags);
            ImGui::Begin("DockSpace", nullptr, window_flags);

            RenderWindowContent();

            ImGui::PopStyleVar();

            ImGui::PopStyleVar(2);

            // Submit the DockSpace
            ImGuiIO &io = ImGui::GetIO();
            if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable) {
                ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
                ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);
            }

            ImGui::End();
        }

        virtual ~ImGuiDockSpace() = default;

    protected:
        virtual void RenderWindowContent() {}

        virtual void ModifyWindowFlags(ImGuiWindowFlags &flags) {}

    private:
    };

    export class ComposableImGuiDockSpace : public ImGuiDockSpace {
    public:
        virtual void RenderWindowContent() override {
            for (auto &contentFunc: mContents) {
                contentFunc();
            }
        }

        void SetContents(std::vector<std::move_only_function<void()>> contents) {
            mContents = std::move(contents);
        }

        void ClearContents() {
            mContents.clear();
        }

        template<typename Func>
        void EmplaceContent(Func &&func) {
            mContents.emplace_back(std::forward<Func>(func));
        }

    private:
        std::vector<std::move_only_function<void()>> mContents;
    };
}
