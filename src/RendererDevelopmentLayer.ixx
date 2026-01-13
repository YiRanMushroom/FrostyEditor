export module RendererDevelopmentLayer;

import Core.Prelude;
import Core.Entrance;
import "SDL3/SDL.h";
import Core.Application;
import ImGui.ImGuiApplication;
import Render.Color;
import ImGui.ImGui;
import Render.Image;
import Core.STLExtension;
import Core.FileSystem;
import Core.Events;
import Vendor.ApplicationAPI;

import Render.GeneratedShaders;
import Render.FramebufferPresenter;
import Render.VirtualTextureManager;
import Render.Renderer2D;
import Render.FontResource;
import Core.Utilities;

import Render.FontRenderer;

using namespace Engine;

export class RendererDevelopmentLayer : public Layer {
public:
    void OnAttach(const std::shared_ptr<Application> &app) override {
        Layer::OnAttach(app);
        auto &swapchain = mApp->GetSwapchainData();

        Renderer2DDescriptor rendererDesc;
        rendererDesc.OutputSize = {swapchain.GetWidth(), swapchain.GetHeight()};
        rendererDesc.VirtualSizeWidth = 1000.f;

        mRenderer = std::make_shared<Renderer2D>(rendererDesc, mApp->GetNvrhiDevice());

        auto info = swapchain.GetFramebufferInfo();

        mPresenter = std::make_shared<FramebufferPresenter>(mApp->GetNvrhiDevice().Get(),
                                                            swapchain.GetFramebufferInfo());

        auto &device = mApp->GetNvrhiDevice();
        auto &commandList = mApp->GetCommandList();

        commandList->open();

        mRedTextureHandle = CreateSolidColorTexture(
            device,
            commandList,
            nvrhi::Color(1.0f, 0.0f, 0.0f, 1.0f),
            "RedTexture"
        );

        mGreenTextureHandle = CreateSolidColorTexture(
            device,
            commandList,
            nvrhi::Color(0.0f, 1.0f, 0.0f, 1.0f),
            "GreenTexture"
        );

        mBlueTextureHandle = CreateSolidColorTexture(
            device,
            commandList,
            nvrhi::Color(0.0f, 0.0f, 1.0f, 1.0f),
            "BlueTexture"
        );

        commandList->close();
        device->executeCommandList(commandList);

        InitializeFontAsync();
    }

    virtual void OnUpdate(std::chrono::duration<float> deltaTime) override {
        Layer::OnUpdate(deltaTime);

        ImGui::Begin("Renderer Development Layer");
        ImGui::Text("Frame rate: %.2f FPS", ImGui::GetIO().Framerate);
        ImGui::End();
    }

    virtual void OnRender(const nvrhi::CommandListHandle &commandList,
                          const nvrhi::FramebufferHandle &framebuffer, uint32_t frameIndex) override {
        mRenderer->BeginRendering();

        // uint32_t texIdRed = mRenderer->RegisterVirtualTextureForThisFrame(mRedTextureHandle);
        // uint32_t texIdGreen = mRenderer->RegisterVirtualTextureForThisFrame(mGreenTextureHandle);
        // uint32_t texIdBlue = mRenderer->RegisterVirtualTextureForThisFrame(mBlueTextureHandle);

        // // Test Triangle commands (Top row, left side)
        // // Colored triangle
        // mRenderer->DrawTriangleColored(
        //     glm::mat3x2(-850.0f, -350.0f, -750.0f, -350.0f, -800.0f, -250.0f),
        //     glm::u8vec4(255, 0, 0, 255)
        // );
        //
        // // Textured triangle with virtual texture
        // mRenderer->DrawTriangleTextureVirtual(
        //     glm::mat3x2(-650.0f, -350.0f, -550.0f, -350.0f, -600.0f, -250.0f),
        //     glm::mat3x2(0.0f, 0.0f, 1.0f, 0.0f, 0.5f, 1.0f),
        //     texIdRed
        // );
        //
        // // Test Quad commands (Top row, center-left)
        // // Colored quad
        // mRenderer->DrawQuadColored(
        //     glm::mat4x2(-450.0f, -350.0f, -350.0f, -350.0f, -350.0f, -250.0f, -450.0f, -250.0f),
        //     glm::u8vec4(0, 255, 0, 255)
        // );
        //
        // // Textured quad with virtual texture
        // mRenderer->DrawQuadTextureVirtual(
        //     glm::mat4x2(-250.0f, -350.0f, -150.0f, -350.0f, -150.0f, -250.0f, -250.0f, -250.0f),
        //     glm::mat4x2(0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f),
        //     texIdGreen
        // );
        //
        // // Test Line commands (Top row, center-right)
        // mRenderer->DrawLine(glm::vec2(50.0f, -350.0f), glm::vec2(150.0f, -350.0f), glm::u8vec4(255, 255, 0, 255));
        // mRenderer->DrawLine(glm::vec2(50.0f, -320.0f), glm::vec2(150.0f, -260.0f),
        //                     glm::u8vec4(0, 0, 255, 255));
        //
        // // Test Circle commands (Top row, right side)
        // mRenderer->DrawCircle(glm::vec2(350.0f, -300.0f), 50.0f, glm::u8vec4(255, 0, 255, 255));
        // mRenderer->DrawCircleTextureVirtual(glm::vec2(550.0f, -300.0f), 50.0f, texIdBlue);
        //
        // // Test Ellipse commands (Middle row, left side)
        // mRenderer->DrawEllipse(glm::vec2(-700.0f, 0.0f), glm::vec2(80.0f, 50.0f), 0.785f,
        //                        glm::u8vec4(0, 255, 255, 255));
        // mRenderer->DrawEllipseTextureVirtual(glm::vec2(-500.0f, 0.0f), glm::vec2(80.0f, 50.0f), 0.0f,
        //                                      texIdRed, glm::u8vec4(255, 255, 255, 200));
        //
        // // Test Ring command (Middle row, center-left)
        // mRenderer->DrawRing(glm::vec2(-250.0f, 0.0f), 60.0f, 40.0f, glm::u8vec4(255, 128, 0, 255));
        //
        // // Test Sector commands (Middle row, center)
        // mRenderer->DrawSector(glm::vec2(0.0f, 0.0f), 60.0f, 0.0f, 3.14159f,
        //                       glm::u8vec4(128, 0, 255, 255));
        // mRenderer->DrawSectorTextureVirtual(glm::vec2(200.0f, 0.0f), 60.0f, 0.0f, 3.14159f,
        //                                     texIdGreen, glm::u8vec4(255, 255, 255, 255));
        //
        // // Test Arc command (Middle row, right side)
        // mRenderer->DrawArc(glm::vec2(500.0f, 0.0f), 60.0f, 12.0f, 0.0f, 4.71239f,
        //                    glm::u8vec4(255, 200, 0, 255));
        //
        // // Test Ellipse Sector commands (Bottom row)
        // mRenderer->DrawEllipseSector(glm::vec2(-500.0f, 300.0f), glm::vec2(80.0f, 50.0f), 0.0f,
        //                              0.0f, 3.14159f, glm::u8vec4(0, 128, 255, 255));
        // mRenderer->DrawEllipseSectorTextureVirtual(glm::vec2(-200.0f, 300.0f), glm::vec2(80.0f, 50.0f), 0.0f,
        //                                            0.0f, 3.14159f, texIdRed);
        //
        // // Test Ellipse Arc command (Bottom row, right side)
        // mRenderer->DrawEllipseArc(glm::vec2(200.0f, 300.0f), glm::vec2(80.0f, 50.0f), 0.785f,
        //                           10.0f, 0.0f, 4.71239f, glm::u8vec4(255, 100, 100, 255));

        mRenderer->DrawTriangleColored(
            glm::mat3x2(0.f, -50.f, -100.f, 50.f, 100.f, 50.f),
            glm::u8vec4(255, 0, 0, 255)
        );

        if (mFontInitializer.IsInitialized()) {
            uint32_t fontTextureID = mRenderer->RegisterVirtualTextureForThisFrame(mFontTexture);

            // render the font atlas in black from -100, -100 to 100, 100

            Engine::DrawTextAsciiCommand textCmd;
            textCmd.SetText(
                        "I Love Furries! \n-- I am Aquafrost.")
                    .SetStartPosition({-120.f, -100.f})
                    .SetEndPosition({120.f, 100.f})
                    .SetClipRegion({-500, 500}, {500, -500})
                    .SetColor({255, 255, 255, 255})
                    .SetFontContext(mFontData.get())
                    .SetFontSize(16.f)
                    .SetVirtualFontTextureId(fontTextureID);

            mRenderer->Draw(textCmd);

            // Engine::ClipRegion clipRegion;
            // clipRegion.Points = {
            //     {-256.f, -256.f},
            //     {256.f, -256.f},
            //     {256.f, 256.f},
            //     {-256.f, 256.f}
            // };
            // clipRegion.PointCount = 4;
            // clipRegion.ClipMode = Engine::ClipMode::ShowInside;
            // int clipRegionId = mRenderer->GetClipRegionManager().RegisterClipRegion(clipRegion);
            //
            // Engine::QuadDrawCommand quadCmd;
            // quadCmd.SetFontAtlas(fontTextureID, mFontData->MTSDFPixelRange)
            //         .SetFirstPoint({-256.f, -256.f})
            //         .SetSecondPoint({256.f, 256.f})
            //         .SetFirstUV({0.f, 0.f})
            //         .SetSecondUV({1.f, 1.f})
            //         .SetTintColor({0, 0, 0, 255})
            //         .SetClipRegionId(clipRegionId);
            //
            // mRenderer->Draw(quadCmd);
        }

        mRenderer->EndRendering();

        // Present the renderer's output to the main framebuffer
        mPresenter->Present(commandList, mRenderer->GetTexture(), framebuffer);
    }

    virtual bool OnEvent(const Event &event) override {
        if ((event.type == SDL_EVENT_WINDOW_RESIZED || event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)
            && event.window.windowID == SDL_GetWindowID(mApp->GetWindow().get())
        ) {
            int width = 0, height = 0;
            SDL_GetWindowSize(SDL_GetWindowFromID(event.window.windowID), &width, &height);
            mRenderer->OnResize(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
        }

        return false;
    }

private:
    std::shared_ptr<Renderer2D> mRenderer;
    std::shared_ptr<FramebufferPresenter> mPresenter;

    nvrhi::TextureHandle CreateSolidColorTexture(nvrhi::IDevice *device, nvrhi::ICommandList *cl, nvrhi::Color color,
                                                 const char *debugName) {
        nvrhi::TextureDesc desc;
        desc.width = 2;
        desc.height = 2;
        desc.format = nvrhi::Format::RGBA8_UNORM;
        desc.debugName = debugName;
        desc.isShaderResource = true;
        desc.initialState = nvrhi::ResourceStates::ShaderResource;
        desc.keepInitialState = true;

        auto tex = device->createTexture(desc);

        auto r = static_cast<uint8_t>(std::clamp(color.r * 255.f, 0.f, 255.f));
        auto g = static_cast<uint8_t>(std::clamp(color.g * 255.f, 0.f, 255.f));
        auto b = static_cast<uint8_t>(std::clamp(color.b * 255.f, 0.f, 255.f));
        auto a = static_cast<uint8_t>(std::clamp(color.a * 255.f, 0.f, 255.f));

        uint32_t pixel = (a << 24) | (b << 16) | (g << 8) | r;
        uint32_t data[4] = {pixel, pixel, pixel, pixel};

        cl->writeTexture(tex, 0, 0, data, 2 * sizeof(uint32_t));

        return tex;
    }

    nvrhi::TextureHandle mRedTextureHandle;
    nvrhi::TextureHandle mGreenTextureHandle;
    nvrhi::TextureHandle mBlueTextureHandle;

    void InitializeFontAsync();

    Engine::Initializer mFontInitializer;
    std::shared_ptr<Engine::FontAtlasData> mFontData;
    nvrhi::TextureHandle mFontTexture;
};

void RendererDevelopmentLayer::InitializeFontAsync() {
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

            Engine::GenerateFontAtlasInfo atlasInfo;
            atlasInfo.FontsToBake.push_back({
                font.get(),
                {msdf_atlas::Charset::ASCII}
            });

            mFontData = GenerateFontAtlas(atlasInfo);

            Engine::SimpleGPUImageDescriptor imageDesc{};
            imageDesc.width = mFontData->AtlasWidth;
            imageDesc.height = mFontData->AtlasHeight;
            imageDesc.imageData = std::span(
                reinterpret_cast<const uint32_t *>(mFontData->AtlasBitmapData.get()), mFontData->PixelCount);
            imageDesc.debugName = "FontAtlasTexture";

            auto Device = mApp->GetNvrhiDevice();
            auto commandList = Device->createCommandList();

            mFontTexture = Engine::UploadImageToGPU(imageDesc, Device, commandList);
        }
    };
}
