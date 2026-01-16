export module Editor.ImGuiRenderViewports;

import Core.Prelude;
import Vendor.ApplicationAPI;
import ImGui.ImGui;

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
}
