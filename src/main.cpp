import Core.Prelude;
import Core.Entrance;
import Vendor.ApplicationAPI;
import "SDL3/SDL.h";
import Core.Application;
import ImGui.ImGuiApplication;
import Render.Color;
import ImGui.ImGui;

namespace
Engine {
    class ImGuiDebugTestLayer : public Layer {
    public:
        void OnUpdate(std::chrono::duration<float> deltaTime) override {
            Layer::OnUpdate(deltaTime);

            static bool show_demo_window = true;
            if (show_demo_window)
                ImGui::ShowDemoWindow(&show_demo_window);

            ImGui::Begin("MyPink Texture Window");
            ImGui::Text("This is my pink texture rendered in ImGui:");
            ImGui::ImageAutoManaged(mImGuiTexture, ImVec2(128, 128));

            // auto& commandList = mApp.get()->GetCommandList();
            // commandList->setTextureState(mMyTexture, nvrhi::AllSubresources, nvrhi::ResourceStates::ShaderResource);

            ImGui::End();
        }

        void OnAttach(const std::shared_ptr<Application> &app) override {
            Layer::OnAttach(app);

            // create a 16 by 16 MyPink texture using NVRHI and get an ImGui texture ID for it
            nvrhi::Color color = Engine::Color::MyPink;
            InitMyTexture();
            mImGuiTexture = ImGui::ImGuiImage::Create(mMyTexture, static_cast<ImGuiApplication *>(mApp.get())->GetImGuiTextureSampler());
        }

        void OnDetach() override {
            mImGuiTexture.Reset();
            mMyTexture.Reset();

            Layer::OnDetach();
        }

        void OnRender(const nvrhi::CommandListHandle &commandList,
                      const nvrhi::FramebufferHandle &framebuffer) override {
        }

    private:
        nvrhi::TextureHandle mMyTexture;
        ImGui::ImGuiImage mImGuiTexture;

        void InitMyTexture() {
            auto& mNvrhiDevice = static_cast<ImGuiApplication *>(mApp.get())->GetNvrhiDevice();
            nvrhi::TextureDesc desc;
            desc.width = 16;
            desc.height = 16;
            desc.format = nvrhi::Format::RGBA8_UNORM;
            desc.debugName = "MyPinkTexture";
            desc.isRenderTarget = false;
            desc.isUAV = false;
            desc.initialState = nvrhi::ResourceStates::ShaderResource;
            desc.keepInitialState = true;

            nvrhi::TextureHandle pinkTexture = mNvrhiDevice->createTexture(desc);

            auto color = Engine::Color::MyPink;
            uint8_t r = static_cast<uint8_t>(color.r * 255.0f);
            uint8_t g = static_cast<uint8_t>(color.g * 255.0f);
            uint8_t b = static_cast<uint8_t>(color.b * 255.0f);
            uint8_t a = static_cast<uint8_t>(color.a * 255.0f);

            std::vector<uint32_t> pixels(16 * 16, (a << 24) | (b << 16) | (g << 8) | r);

            auto& mCommandList = static_cast<ImGuiApplication *>(mApp.get())->GetCommandList();

            mCommandList->open();
            mCommandList->writeTexture(pinkTexture, 0, 0, pixels.data(), 16 * sizeof(uint32_t));
            mCommandList->close();
            mNvrhiDevice->executeCommandList(mCommandList);

            mMyTexture = std::move(pinkTexture);
        }
    };

    int Main(int argc, char **argv) {
        auto app = std::make_shared<ImGuiApplication>();
        app->Init({
            .Title = "Frosty Engine App"
        });
        app->EmplaceLayer<ImGuiDebugTestLayer>();
        app->Run();
        app->DetachAllLayers();
        app->Destroy();
        return 0;
    }
}
