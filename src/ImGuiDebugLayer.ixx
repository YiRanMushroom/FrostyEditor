export module ImGuiDebugLayer;

import Core.Prelude;
import Core.Entrance;
import Vendor.ApplicationAPI;
import "SDL3/SDL.h";
import Core.Application;
import ImGui.ImGuiApplication;
import Render.Color;
import ImGui.ImGui;
import Render.Image;
import Core.STLExtension;
import Core.FileSystem;
import Core.Coroutine;

using namespace Engine;

export class ImGuiDebugTestLayer : public Layer {
public:
    void OnUpdate(std::chrono::duration<float> deltaTime) override {
        Layer::OnUpdate(deltaTime);

        static bool show_demo_window = true;
        if (show_demo_window)
            ImGui::ShowDemoWindow(&show_demo_window);

        ImGui::Begin("MyPink Texture Window");
        ImGui::Text("This is my pink texture rendered in ImGui:");
        ImGui::ImageAutoManaged(mImGuiTexture, ImVec2(128, 128));


        if (ImGui::Button("Load More Images")) {
            mLoadingFutures.push_back(OpenDialogAndLoadImagesAsync().IntoFuture());
        }

        ImGui::End();

        OnFrameEnded([this] {
            std::erase_if(
                mLoadedImages,
                [](const auto &pair) {
                    return !pair.second;
                }
            );
        });

        std::erase_if(
            mLoadingFutures,
            [this](std::future<std::vector<ImGui::ImGuiImage>> &future) {
                if (future.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                    auto images = future.get();
                    for (auto &img: images) {
                        mLoadedImages.emplace_back(std::move(img), true);
                    }
                    return true;
                }
                return false;
            }
        );

        for (size_t i = 0; i < mLoadedImages.size(); ++i) {
            auto desc = mLoadedImages[i].first.GetTextureDesc();
            ImGui::Begin(("Loaded Image " + std::to_string(i)).c_str(), &mLoadedImages[i].second);
            ImGui::Text("Dimensions: %d x %d", desc.width, desc.height);

            ImVec2 displaySize = ImVec2(static_cast<float>(desc.width), static_cast<float>(desc.height));

            if (desc.width > 1920 || desc.height > 1080) {
                float aspectRatio = static_cast<float>(desc.width) / static_cast<float>(desc.height);
                if (aspectRatio > (1920.0f / 1080.0f)) {
                    displaySize.x = 1920.0f;
                    displaySize.y = 1920.0f / aspectRatio;
                } else {
                    displaySize.y = 1080.0f;
                    displaySize.x = 1080.0f * aspectRatio;
                }
                ImGui::Text("Image is too large, displaying scaled down version:");
            }

            ImGui::ImageAutoManaged(mLoadedImages[i].first,
                                    displaySize);
            ImGui::End();
        }
    }

    void OnAttach(const std::shared_ptr<Application> &app) override {
        Layer::OnAttach(app);

        InitMyTexture();
        mImGuiTexture = ImGui::ImGuiImage::Create(
            mMyTexture, static_cast<ImGuiApplication *>(mApp.get())->GetImGuiTextureSampler());
    }

    void OnDetach() override {
        mImGuiTexture.Reset();
        mMyTexture.Reset();

        Layer::OnDetach();
    }

    void OnRender(const nvrhi::CommandListHandle &commandList,
                  const nvrhi::FramebufferHandle &framebuffer, uint32_t) override {}

    Engine::Awaitable<std::vector<ImGui::ImGuiImage>> OpenDialogAndLoadImagesAsync() {
        SDL_DialogFileFilter filters[] = {
            {.name = "PNG Images", .pattern = "png"},
            {.name = "JPEG Images", .pattern = "jpg;jpeg"},
            {.name = "All Files", .pattern = "*"}
        };

        auto paths = co_await OpenFileDialogAsync(
            mApp->GetWindow().get(),
            filters
        );

        std::vector<CPUImage> images;
        for (const auto &path: paths) {
            images.push_back(co_await LoadImageFromFileAsync(path));
        }

        auto device = mApp->GetNvrhiDevice();
        auto commandList = device->createCommandList();

        auto gpuImages = UploadImagesToGPU(
            images | std::views::transform([](const CPUImage &it) {
                return it.GetGPUDescriptor();
            }) | std::ranges::to<std::vector<GPUImageDescriptor>>(),
            device.Get(),
            commandList);

        std::vector<ImGui::ImGuiImage> imguiImages;
        for (const auto &tex : gpuImages) {
            imguiImages.push_back(
                ImGui::ImGuiImage::Create(
                    tex,
                    static_cast<ImGuiApplication *>(mApp.get())->GetImGuiTextureSampler()));
        }

        co_return imguiImages;
    }

private:
    nvrhi::TextureHandle mMyTexture;
    ImGui::ImGuiImage mImGuiTexture;

    std::vector<std::pair<ImGui::ImGuiImage, bool>> mLoadedImages;
    std::vector<std::future<std::vector<ImGui::ImGuiImage>>> mLoadingFutures;

    void InitMyTexture() {
        auto &mNvrhiDevice = mApp.get()->GetNvrhiDevice();

        auto color = Engine::Color::MyPink;
        uint8_t r = static_cast<uint8_t>(color.r * 255.0f);
        uint8_t g = static_cast<uint8_t>(color.g * 255.0f);
        uint8_t b = static_cast<uint8_t>(color.b * 255.0f);
        uint8_t a = static_cast<uint8_t>(color.a * 255.0f);

        std::vector<uint32_t> pixels(15 * 15, (a << 24) | (b << 16) | (g << 8) | r);

        nvrhi::TextureHandle pinkTexture = UploadImageToGPU(
            GPUImageDescriptor{
                .width = 15,
                .height = 15,
                .imageData = pixels
            },
            mNvrhiDevice,
            mApp.get()->GetCommandList()
        );

        mMyTexture = std::move(pinkTexture);
    }
};
