export module RendererDevelopmentLayer;

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
import Core.Events;

import Render.GeneratedShaders;

using namespace Engine;

struct Vertex2D {
    float Position[2];
};

class DynamicVertexBuffer {
public:
    DynamicVertexBuffer(nvrhi::IDevice *device, const std::string &debugName)
        : mDevice(device), mDebugName(debugName), mCapacity(0) {}

    void EnsureCapacity(uint64_t neededVertexCount) {
        if (neededVertexCount <= mCapacity) return;

        // Exponential growth strategy: 1.5x
        uint64_t newCapacity = std::max(neededVertexCount, mCapacity + mCapacity / 2);
        if (newCapacity < 1024) newCapacity = 1024; // Minimum initial capacity

        mDevice->waitForIdle();

        nvrhi::BufferDesc desc;
        desc.byteSize = newCapacity * sizeof(Vertex2D);
        desc.isVertexBuffer = true;
        desc.debugName = mDebugName;
        desc.initialState = nvrhi::ResourceStates::VertexBuffer;
        desc.keepInitialState = true;

        mHandle = mDevice->createBuffer(desc);
        mCapacity = newCapacity;
    }

    nvrhi::IBuffer *GetHandle() const { return mHandle.Get(); }
    uint64_t GetCapacity() const { return mCapacity; }

private:
    nvrhi::DeviceHandle mDevice;
    nvrhi::BufferHandle mHandle;
    std::string mDebugName;
    uint64_t mCapacity;
};

class FramebufferPresenter {
public:
    FramebufferPresenter(nvrhi::IDevice *device, const nvrhi::FramebufferInfo &targetFramebufferInfo)
        : mDevice(device) {
        CreateResources(targetFramebufferInfo);
    }

    void Present(nvrhi::ICommandList *commandList, nvrhi::ITexture *sourceTexture,
                nvrhi::IFramebuffer *targetFramebuffer) {
        // Update binding set with the current source texture
        nvrhi::BindingSetDesc setDesc;
        setDesc.bindings = {
            nvrhi::BindingSetItem::Texture_SRV(0, sourceTexture),
            nvrhi::BindingSetItem::Sampler(0, mSampler)
        };
        auto bindingSet = mDevice->createBindingSet(setDesc, mBindingLayout);

        commandList->setResourceStatesForFramebuffer(targetFramebuffer);
        commandList->setResourceStatesForBindingSet(bindingSet);

        nvrhi::GraphicsState state;
        state.pipeline = mPipeline;
        state.framebuffer = targetFramebuffer;
        state.bindings = {bindingSet};
        state.viewport.addViewportAndScissorRect(targetFramebuffer->getFramebufferInfo().getViewport());

        commandList->setGraphicsState(state);
        commandList->draw(nvrhi::DrawArguments().setVertexCount(3));
    }

private:
    nvrhi::DeviceHandle mDevice;

    nvrhi::SamplerHandle mSampler;
    nvrhi::BindingLayoutHandle mBindingLayout;
    nvrhi::GraphicsPipelineHandle mPipeline;

    void CreateResources(const nvrhi::FramebufferInfo &targetFramebufferInfo) {
        mSampler = mDevice->createSampler(nvrhi::SamplerDesc()
            .setAllAddressModes(nvrhi::SamplerAddressMode::Clamp)
            .setAllFilters(true));

        // Create Binding Layout
        nvrhi::BindingLayoutDesc layoutDesc;
        layoutDesc.visibility = nvrhi::ShaderType::Pixel;
        layoutDesc.bindings = {
            nvrhi::BindingLayoutItem::Texture_SRV(0),
            nvrhi::BindingLayoutItem::Sampler(0)
        };
        mBindingLayout = mDevice->createBindingLayout(layoutDesc);

        // Create Pipeline
        nvrhi::ShaderDesc vsDesc;
        vsDesc.shaderType = nvrhi::ShaderType::Vertex;
        vsDesc.entryName = "main";
        nvrhi::ShaderHandle vs = mDevice->createShader(vsDesc,
            GeneratedShaders::copy_to_main_framebuffer_vs.data(),
            GeneratedShaders::copy_to_main_framebuffer_vs.size());

        nvrhi::ShaderDesc psDesc;
        psDesc.shaderType = nvrhi::ShaderType::Pixel;
        psDesc.entryName = "main";
        nvrhi::ShaderHandle ps = mDevice->createShader(psDesc,
            GeneratedShaders::copy_to_main_framebuffer_ps.data(),
            GeneratedShaders::copy_to_main_framebuffer_ps.size());

        nvrhi::GraphicsPipelineDesc pipeDesc;
        pipeDesc.VS = vs;
        pipeDesc.PS = ps;
        pipeDesc.bindingLayouts = {mBindingLayout};
        pipeDesc.primType = nvrhi::PrimitiveType::TriangleList;

        pipeDesc.renderState.blendState.targets[0].blendEnable = true;
        pipeDesc.renderState.blendState.targets[0].srcBlend = nvrhi::BlendFactor::SrcAlpha;
        pipeDesc.renderState.blendState.targets[0].destBlend = nvrhi::BlendFactor::InvSrcAlpha;
        pipeDesc.renderState.blendState.targets[0].srcBlendAlpha = nvrhi::BlendFactor::One;
        pipeDesc.renderState.blendState.targets[0].destBlendAlpha = nvrhi::BlendFactor::InvSrcAlpha;
        pipeDesc.renderState.blendState.targets[0].colorWriteMask = nvrhi::ColorMask::All;

        pipeDesc.renderState.rasterState.cullMode = nvrhi::RasterCullMode::None;
        pipeDesc.renderState.depthStencilState.depthTestEnable = false;

        mPipeline = mDevice->createGraphicsPipeline(pipeDesc, targetFramebufferInfo);
    }
};

class Renderer2D {
public:
    Renderer2D(nvrhi::IDevice *device, uint32_t width, uint32_t height, uint32_t bufferCount)
        : mDevice(device), mWidth(width), mHeight(height), mBufferCount(bufferCount),
          mTriangleBuffer(device, "Renderer2D_TriangleVB") {
        CreateResources();
        CreatePipeline();
    }

    void StartRendering();

    void DrawTriangle(Vertex2D v1, Vertex2D v2, Vertex2D v3);

    void EndRendering();

    void OnResize(uint32_t width, uint32_t height);

    nvrhi::ITexture *GetCurrentTexture() const {
        return mTextures[mFrameIndex % mBufferCount].Get();
    }

    const std::vector<nvrhi::TextureHandle> &GetTextures() const {
        return mTextures;
    }

    uint64_t GetFrameIndex() const { return mFrameIndex; }

    void AdvanceFrame() { mFrameIndex++; }

private:
    nvrhi::DeviceHandle mDevice;
    uint32_t mWidth, mHeight, mBufferCount;
    uint64_t mFrameIndex = 0;

    std::vector<nvrhi::TextureHandle> mTextures;
    std::vector<nvrhi::FramebufferHandle> mFramebuffers;

    nvrhi::GraphicsPipelineHandle mPipeline;
    nvrhi::InputLayoutHandle mInputLayout;

    nvrhi::CommandListHandle mCommandList;

    DynamicVertexBuffer mTriangleBuffer;
    std::vector<Vertex2D> mTriangleVertices;

    void CreateResources();

    void CreatePipeline();
};

void Renderer2D::StartRendering() {
    mCommandList->open();
    mTriangleVertices.clear();

    uint32_t slot = mFrameIndex % mBufferCount;
    mCommandList->setResourceStatesForFramebuffer(mFramebuffers[slot]);
    mCommandList->clearTextureFloat(mTextures[slot],
                                    nvrhi::AllSubresources, nvrhi::Color(0.f, 0.f, 0.f, 0.f));
}

void Renderer2D::DrawTriangle(Vertex2D v1, Vertex2D v2, Vertex2D v3) {
    mTriangleVertices.push_back(v1);
    mTriangleVertices.push_back(v2);
    mTriangleVertices.push_back(v3);
}

void Renderer2D::EndRendering() {
    if (!mTriangleVertices.empty()) {
        mTriangleBuffer.EnsureCapacity(mTriangleVertices.size());
        mCommandList->writeBuffer(mTriangleBuffer.GetHandle(), mTriangleVertices.data(),
                                  mTriangleVertices.size() * sizeof(Vertex2D));

        uint32_t slot = mFrameIndex % mBufferCount;
        nvrhi::GraphicsState state;
        state.pipeline = mPipeline;
        state.framebuffer = mFramebuffers[slot];
        state.vertexBuffers = {{mTriangleBuffer.GetHandle(), 0, 0}};
        state.viewport.addViewportAndScissorRect(
            nvrhi::Viewport(static_cast<float>(mWidth), static_cast<float>(mHeight)));

        mCommandList->setGraphicsState(state);

        nvrhi::DrawArguments args;
        args.vertexCount = static_cast<uint32_t>(mTriangleVertices.size());
        mCommandList->draw(args);
    }

    mCommandList->close();
    mDevice->executeCommandList(mCommandList);
}

void Renderer2D::OnResize(uint32_t width, uint32_t height) {
    if (width == mWidth && height == mHeight) return;
    mDevice->waitForIdle();

    mWidth = width;
    mHeight = height;
    mTextures.clear();
    mFramebuffers.clear();

    CreateResources();
}

void Renderer2D::CreateResources() {
    nvrhi::TextureDesc texDesc;
    texDesc.width = mWidth;
    texDesc.height = mHeight;
    texDesc.format = nvrhi::Format::RGBA8_UNORM;
    texDesc.isRenderTarget = true;
    texDesc.isShaderResource = true;
    texDesc.initialState = nvrhi::ResourceStates::ShaderResource;
    texDesc.keepInitialState = true;
    texDesc.clearValue = nvrhi::Color(0.f, 0.f, 0.f, 0.f);

    for (uint32_t i = 0; i < mBufferCount; ++i) {
        auto tex = mDevice->createTexture(texDesc);
        mTextures.push_back(tex);
        mFramebuffers.push_back(mDevice->createFramebuffer(
            nvrhi::FramebufferDesc().addColorAttachment(tex)));
    }

    if (!mCommandList) {
        mCommandList = mDevice->createCommandList();
    }
}

void Renderer2D::CreatePipeline() {
    nvrhi::ShaderDesc vsDesc;
    vsDesc.shaderType = nvrhi::ShaderType::Vertex;
    vsDesc.entryName = "main";
    vsDesc.debugName = "Triangle_VS";
    nvrhi::ShaderHandle vs = mDevice->createShader(vsDesc,
        GeneratedShaders::simple_vb_triangle_vs.data(),
        GeneratedShaders::simple_vb_triangle_vs.size());

    nvrhi::ShaderDesc psDesc;
    psDesc.shaderType = nvrhi::ShaderType::Pixel;
    psDesc.entryName = "main";
    psDesc.debugName = "Triangle_PS";
    nvrhi::ShaderHandle ps = mDevice->createShader(psDesc,
        GeneratedShaders::simple_vb_triangle_ps.data(),
        GeneratedShaders::simple_vb_triangle_ps.size());

    nvrhi::VertexAttributeDesc posAttr;
    posAttr.name = "POSITION";
    posAttr.format = nvrhi::Format::RG32_FLOAT;
    posAttr.bufferIndex = 0;
    posAttr.offset = 0;
    posAttr.elementStride = sizeof(Vertex2D);

    mInputLayout = mDevice->createInputLayout(&posAttr, 1, vs);

    nvrhi::GraphicsPipelineDesc pipeDesc;
    pipeDesc.VS = vs;
    pipeDesc.PS = ps;
    pipeDesc.inputLayout = mInputLayout;
    pipeDesc.primType = nvrhi::PrimitiveType::TriangleList;

    pipeDesc.renderState.blendState.targets[0].blendEnable = true;
    pipeDesc.renderState.blendState.targets[0].srcBlend = nvrhi::BlendFactor::SrcAlpha;
    pipeDesc.renderState.blendState.targets[0].destBlend = nvrhi::BlendFactor::InvSrcAlpha;
    pipeDesc.renderState.blendState.targets[0].srcBlendAlpha = nvrhi::BlendFactor::One;
    pipeDesc.renderState.blendState.targets[0].destBlendAlpha = nvrhi::BlendFactor::InvSrcAlpha;
    pipeDesc.renderState.blendState.targets[0].colorWriteMask = nvrhi::ColorMask::All;

    pipeDesc.renderState.rasterState.cullMode = nvrhi::RasterCullMode::None;
    pipeDesc.renderState.depthStencilState.depthTestEnable = false;

    mPipeline = mDevice->createGraphicsPipeline(pipeDesc, mFramebuffers[0]->getFramebufferInfo());
}

export class RendererDevelopmentLayer : public Layer {
public:
    virtual void OnAttach(const std::shared_ptr<Application> &app) override {
        Layer::OnAttach(app);
        auto &swapchain = mApp->GetSwapchainData();

        mRenderer = std::make_shared<Renderer2D>(mApp->GetNvrhiDevice().Get(),
                                                 swapchain.GetWidth(),
                                                 swapchain.GetHeight(), 2);

        mPresenter = std::make_shared<FramebufferPresenter>(mApp->GetNvrhiDevice().Get(),
                                                            swapchain.GetFramebufferInfo());
    }

    virtual void OnRender(const nvrhi::CommandListHandle &commandList,
                          const nvrhi::FramebufferHandle &framebuffer) override {
        mRenderer->StartRendering();
        // Test drawing 11 triangles to verify capacity growth, from left to right, no overlap
        for (int i = -5; i <= 5; ++i) {
            mRenderer->DrawTriangle({0.1f * i, -0.5f}, {0.1f * i + 0.05f, 0.5f}, {0.1f * i + 0.1f, -0.5f});
        }
        mRenderer->EndRendering();

        // Present the renderer's output to the main framebuffer
        mPresenter->Present(commandList, mRenderer->GetCurrentTexture(), framebuffer);

        mRenderer->AdvanceFrame();
    }

    virtual bool OnEvent(const Event &event) override {
        if (event.type == SDL_EVENT_WINDOW_RESIZED || event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
            int width = 0, height = 0;
            SDL_GetWindowSize(SDL_GetWindowFromID(event.window.windowID), &width, &height);
            mRenderer->OnResize(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
        }

        return false;
    }

private:
    std::shared_ptr<Renderer2D> mRenderer;
    std::shared_ptr<FramebufferPresenter> mPresenter;
};
