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

class Renderer2D {
public:
    Renderer2D(nvrhi::IDevice *device, uint32_t width, uint32_t height, uint32_t bufferCount,
               const nvrhi::FramebufferInfo &mainFramebufferInfo)
        : mDevice(device), mWidth(width), mHeight(height), mBufferCount(bufferCount),
          mTriangleBuffer(device, "Renderer2D_TriangleVB") {
        CreateResources();
        CreatePipelines(mainFramebufferInfo);
    }

    void StartRendering();

    void DrawTriangle(Vertex2D v1, Vertex2D v2, Vertex2D v3);

    void EndRendering();

    void PresentToMain(nvrhi::ICommandList *commandList, nvrhi::IFramebuffer *mainFramebuffer);

    void OnResize(uint32_t width, uint32_t height);

    nvrhi::IFramebuffer *GetCurrentInternalFramebuffer() {
        return mInternalFramebuffers[mFrameIndex % mBufferCount].Get();
    }

private:
    nvrhi::DeviceHandle mDevice;
    uint32_t mWidth, mHeight, mBufferCount;
    uint64_t mFrameIndex = 0;

    std::vector<nvrhi::TextureHandle> mInternalTextures;
    std::vector<nvrhi::FramebufferHandle> mInternalFramebuffers;
    std::vector<nvrhi::BindingSetHandle> mBlitBindingSets;

    nvrhi::SamplerHandle mSampler;
    nvrhi::BindingLayoutHandle mBlitBindingLayout;
    nvrhi::GraphicsPipelineHandle mBlitPipeline;

    nvrhi::GraphicsPipelineHandle mTrianglePipeline;
    nvrhi::InputLayoutHandle mTriangleInputLayout;

    nvrhi::CommandListHandle mCommandList;

    DynamicVertexBuffer mTriangleBuffer;
    std::vector<Vertex2D> mTriangleVertices;

    void CreateResources();

    void CreatePipelines(const nvrhi::FramebufferInfo &mainFramebufferInfo);
};

void Renderer2D::StartRendering() {
    mCommandList->open();
    mTriangleVertices.clear();

    auto currentFB = GetCurrentInternalFramebuffer();
    mCommandList->setResourceStatesForFramebuffer(currentFB);
    mCommandList->clearTextureFloat(mInternalTextures[mFrameIndex % mBufferCount],
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

        nvrhi::GraphicsState state;
        state.pipeline = mTrianglePipeline;
        state.framebuffer = GetCurrentInternalFramebuffer();
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

void Renderer2D::PresentToMain(nvrhi::ICommandList *commandList, nvrhi::IFramebuffer *mainFramebuffer) {
    uint32_t slot = mFrameIndex % mBufferCount;

    commandList->setResourceStatesForFramebuffer(mainFramebuffer);
    commandList->setResourceStatesForBindingSet(mBlitBindingSets[slot]);

    nvrhi::GraphicsState state;
    state.pipeline = mBlitPipeline;
    state.framebuffer = mainFramebuffer;
    state.bindings = {mBlitBindingSets[slot]};
    state.viewport.addViewportAndScissorRect(mainFramebuffer->getFramebufferInfo().getViewport());

    commandList->setGraphicsState(state);
    commandList->draw(nvrhi::DrawArguments().setVertexCount(3));

    mFrameIndex++;
}

void Renderer2D::OnResize(uint32_t width, uint32_t height) {
    if (width == mWidth && height == mHeight) return;
    mDevice->waitForIdle();

    mWidth = width;
    mHeight = height;
    mInternalTextures.clear();
    mInternalFramebuffers.clear();
    mBlitBindingSets.clear();

    CreateResources();
    for (uint32_t i = 0; i < mBufferCount; ++i) {
        nvrhi::BindingSetDesc setDesc;
        setDesc.bindings = {
            nvrhi::BindingSetItem::Texture_SRV(0, mInternalTextures[i]),
            nvrhi::BindingSetItem::Sampler(0, mSampler)
        };
        mBlitBindingSets.push_back(mDevice->createBindingSet(setDesc, mBlitBindingLayout));
    }
}

void Renderer2D::CreateResources() {
    mSampler = mDevice->createSampler(nvrhi::SamplerDesc()
        .setAllAddressModes(nvrhi::SamplerAddressMode::Clamp)
        .setAllFilters(true));

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
        mInternalTextures.push_back(tex);
        mInternalFramebuffers.push_back(mDevice->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(tex)));
    }
    mCommandList = mDevice->createCommandList();
}

void Renderer2D::CreatePipelines(const nvrhi::FramebufferInfo &mainFramebufferInfo) {
    // -------------------------------------------------------------------------
    // 1. Create Blit Binding Layout
    // -------------------------------------------------------------------------
    nvrhi::BindingLayoutDesc layoutDesc;
    layoutDesc.visibility = nvrhi::ShaderType::Pixel;
    layoutDesc.bindings = {
        nvrhi::BindingLayoutItem::Texture_SRV(0), // t0: Texture
        nvrhi::BindingLayoutItem::Sampler(0) // s0: Sampler
    };

    mBlitBindingLayout = mDevice->createBindingLayout(layoutDesc);

    // -------------------------------------------------------------------------
    // 2. Create Blit Binding Sets
    // -------------------------------------------------------------------------
    mBlitBindingSets.clear();
    for (uint32_t i = 0; i < mBufferCount; ++i) {
        nvrhi::BindingSetDesc setDesc;
        setDesc.bindings = {
            nvrhi::BindingSetItem::Texture_SRV(0, mInternalTextures[i]),
            nvrhi::BindingSetItem::Sampler(0, mSampler)
        };

        mBlitBindingSets.push_back(mDevice->createBindingSet(setDesc, mBlitBindingLayout));
    }

    // -------------------------------------------------------------------------
    // 3. Create Triangle Pipeline (Internal Rendering)
    // -------------------------------------------------------------------------
    nvrhi::ShaderDesc triVSDesc;
    triVSDesc.shaderType = nvrhi::ShaderType::Vertex;
    triVSDesc.entryName = "main";
    triVSDesc.debugName = "Triangle_VS";
    nvrhi::ShaderHandle triVS = mDevice->createShader(triVSDesc, GeneratedShaders::simple_vb_triangle_vs.data(),
                                                      GeneratedShaders::simple_vb_triangle_vs.size());

    nvrhi::ShaderDesc triPSDesc;
    triPSDesc.shaderType = nvrhi::ShaderType::Pixel;
    triPSDesc.entryName = "main";
    triPSDesc.debugName = "Triangle_PS";
    nvrhi::ShaderHandle triPS = mDevice->createShader(triPSDesc, GeneratedShaders::simple_vb_triangle_ps.data(),
                                                      GeneratedShaders::simple_vb_triangle_ps.size());

    nvrhi::VertexAttributeDesc posAttr;
    posAttr.name = "POSITION";
    posAttr.format = nvrhi::Format::RG32_FLOAT;
    posAttr.bufferIndex = 0;
    posAttr.offset = 0;
    posAttr.elementStride = sizeof(Vertex2D);

    mTriangleInputLayout = mDevice->createInputLayout(&posAttr, 1, triVS);

    nvrhi::GraphicsPipelineDesc triPipeDesc;
    triPipeDesc.VS = triVS;
    triPipeDesc.PS = triPS;
    triPipeDesc.inputLayout = mTriangleInputLayout;
    triPipeDesc.primType = nvrhi::PrimitiveType::TriangleList;

    triPipeDesc.renderState.blendState.targets[0].blendEnable = true;
    triPipeDesc.renderState.blendState.targets[0].srcBlend = nvrhi::BlendFactor::SrcAlpha;
    triPipeDesc.renderState.blendState.targets[0].destBlend = nvrhi::BlendFactor::InvSrcAlpha;
    triPipeDesc.renderState.blendState.targets[0].srcBlendAlpha = nvrhi::BlendFactor::One;
    triPipeDesc.renderState.blendState.targets[0].destBlendAlpha = nvrhi::BlendFactor::InvSrcAlpha;
    triPipeDesc.renderState.blendState.targets[0].colorWriteMask = nvrhi::ColorMask::All;

    triPipeDesc.renderState.rasterState.cullMode = nvrhi::RasterCullMode::None;
    triPipeDesc.renderState.depthStencilState.depthTestEnable = false;

    mTrianglePipeline = mDevice->createGraphicsPipeline(triPipeDesc, mInternalFramebuffers[0]->getFramebufferInfo());

    // -------------------------------------------------------------------------
    // 4. Create Blit Pipeline (Output Rendering)
    // -------------------------------------------------------------------------
    nvrhi::ShaderDesc blitVSDesc;
    blitVSDesc.shaderType = nvrhi::ShaderType::Vertex;
    blitVSDesc.entryName = "main";
    nvrhi::ShaderHandle blitVS = mDevice->createShader(blitVSDesc, GeneratedShaders::copy_to_main_framebuffer_vs.data(),
                                                       GeneratedShaders::copy_to_main_framebuffer_vs.size());

    nvrhi::ShaderDesc blitPSDesc;
    blitPSDesc.shaderType = nvrhi::ShaderType::Pixel;
    blitPSDesc.entryName = "main";
    nvrhi::ShaderHandle blitPS = mDevice->createShader(blitPSDesc, GeneratedShaders::copy_to_main_framebuffer_ps.data(),
                                                       GeneratedShaders::copy_to_main_framebuffer_ps.size());

    nvrhi::GraphicsPipelineDesc blitPipeDesc;
    blitPipeDesc.VS = blitVS;
    blitPipeDesc.PS = blitPS;
    blitPipeDesc.bindingLayouts = {mBlitBindingLayout};
    blitPipeDesc.primType = nvrhi::PrimitiveType::TriangleList;

    blitPipeDesc.renderState.blendState.targets[0].blendEnable = true;
    blitPipeDesc.renderState.blendState.targets[0].srcBlend = nvrhi::BlendFactor::SrcAlpha;
    blitPipeDesc.renderState.blendState.targets[0].destBlend = nvrhi::BlendFactor::InvSrcAlpha;
    blitPipeDesc.renderState.blendState.targets[0].srcBlendAlpha = nvrhi::BlendFactor::One;
    blitPipeDesc.renderState.blendState.targets[0].destBlendAlpha = nvrhi::BlendFactor::InvSrcAlpha;
    blitPipeDesc.renderState.blendState.targets[0].colorWriteMask = nvrhi::ColorMask::All;

    blitPipeDesc.renderState.rasterState.cullMode = nvrhi::RasterCullMode::None;
    blitPipeDesc.renderState.depthStencilState.depthTestEnable = false;

    mBlitPipeline = mDevice->createGraphicsPipeline(blitPipeDesc, mainFramebufferInfo);
}


export class RendererDevelopmentLayer : public Layer {
public:
    virtual void OnAttach(const std::shared_ptr<Application> &app) override {
        Layer::OnAttach(app);
        auto &swapchain = mApp->GetSwapchainData();
        mRenderer = std::make_shared<Renderer2D>(mApp->GetNvrhiDevice().Get(),
                                                 swapchain.GetWidth(),
                                                 swapchain.GetHeight(), 2,
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
        mRenderer->PresentToMain(commandList, framebuffer);
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
};
