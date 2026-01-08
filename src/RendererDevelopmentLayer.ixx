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
import <cassert>;
import <cstddef>;

using namespace Engine;

export class FramebufferPresenter {
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

export struct TriangleVertexData {
    std::array<float, 2> position;
    std::array<float, 2> texCoords;
    uint32_t constantIndex;
    uint32_t padding[3];
};

export struct SpriteData {
    uint32_t tintColor;
    int32_t textureIndex; // if < 0, no texture
};

export struct TriangleBasedRenderingCommandList;

export struct TriangleBasedInstanceRenderingData {
    struct VertexPosition {
        float x, y;
        float u, v;
    };

    std::array<VertexPosition, 4> Vertices;
    bool IsQuad;
    int VirtualTextureID;
    uint32_t TintColor;
    int Depth;

    static TriangleBasedInstanceRenderingData Triangle(const VertexPosition &v0,
                                                       const VertexPosition &v1,
                                                       const VertexPosition &v2,
                                                       int textureIndex,
                                                       uint32_t tintColor, int depth = 0) {
        TriangleBasedInstanceRenderingData data;
        data.Vertices[0] = v0;
        data.Vertices[1] = v1;
        data.Vertices[2] = v2;
        data.IsQuad = false;
        data.VirtualTextureID = textureIndex;
        data.TintColor = tintColor;
        data.Depth = depth;
        return data;
    }

    static TriangleBasedInstanceRenderingData Quad(const VertexPosition &v0,
                                                   const VertexPosition &v1,
                                                   const VertexPosition &v2,
                                                   const VertexPosition &v3,
                                                   int virtualTextureID,
                                                   uint32_t tintColor, int depth = 0) {
        TriangleBasedInstanceRenderingData data;
        data.Vertices[0] = v0;
        data.Vertices[1] = v1;
        data.Vertices[2] = v2;
        data.Vertices[3] = v3;
        data.IsQuad = true;
        data.VirtualTextureID = virtualTextureID;
        data.TintColor = tintColor;
        data.Depth = depth;
        return data;
    }
};

static_assert(std::is_trivially_move_assignable_v<TriangleBasedInstanceRenderingData>,
              "TriangleBasedInstanceRenderingData must be trivially move assignable");
static_assert(std::is_trivially_destructible_v<TriangleBasedInstanceRenderingData>,
              "TriangleBasedInstanceRenderingData must be trivially destructible");


struct TriangleRendererSubmissionData {
    std::vector<TriangleVertexData> VertexData;
    std::vector<uint32_t> IndexData;
    std::vector<SpriteData> InstanceData;

    TriangleRendererSubmissionData() = default;

    TriangleRendererSubmissionData(TriangleRendererSubmissionData &&) = default;

    TriangleRendererSubmissionData &operator=(TriangleRendererSubmissionData &&) = default;

    void Clear() {
        VertexData.clear();
        IndexData.clear();
        InstanceData.clear();
    }
};

export class PersistentVirtualTextureManager {
public:
    explicit PersistentVirtualTextureManager(nvrhi::IDevice *device, uint32_t initialMax = 65536)
        : mDevice(device), mMaxTextures(initialMax) {
        mBindingSetDesc.bindings.reserve(mMaxTextures);
    }

    uint32_t RegisterTexture(nvrhi::TextureHandle texture) {
        if (!texture) return static_cast<uint32_t>(-1);

        auto it = mTextureToVirtualID.find(texture.Get());
        if (it != mTextureToVirtualID.end()) {
            return it->second;
        }

        if (mVirtualTextures.size() >= mMaxTextures) {
            throw Engine::RuntimeException(
                "VirtualTextureManager: Capacity reached. Call Optimize() or increase limit.");
        }

        uint32_t newID = static_cast<uint32_t>(mVirtualTextures.size());
        mVirtualTextures.push_back(texture);
        mTextureToVirtualID[texture.Get()] = newID;

        mBindingSetDesc.addItem(nvrhi::BindingSetItem::Texture_SRV(0, texture)
            .setArrayElement(newID));

        mIsDirty = true;
        return newID;
    }

    void Optimize() {
        uint32_t hardLimit = 1 << 18;
        if (mMaxTextures < hardLimit) {
            mMaxTextures = std::min(mMaxTextures * 2, hardLimit);
        }

        Reset();
    }

    nvrhi::BindingSetHandle GetBindingSet(nvrhi::IBindingLayout *layout) {
        if (mVirtualTextures.empty()) {
            return nullptr;
        }

        if (mIsDirty || !mCurrentBindingSet) {
            mCurrentBindingSet = mDevice->createBindingSet(mBindingSetDesc, layout);
            mIsDirty = false;
        }

        return mCurrentBindingSet;
    }

    bool IsSubOptimal() const {
        return mVirtualTextures.size() >= mMaxTextures * 3 / 4;
    }

    void Reset() {
        mVirtualTextures.clear();
        mTextureToVirtualID.clear();
        mBindingSetDesc.bindings.clear();
        mBindingSetDesc.bindings.reserve(mMaxTextures);
        mCurrentBindingSet = nullptr;
        mIsDirty = true;
    }

    uint32_t GetCurrentSize() const { return static_cast<uint32_t>(mVirtualTextures.size()); }
    uint32_t GetCapacity() const { return mMaxTextures; }

private:
    nvrhi::DeviceHandle mDevice;
    uint32_t mMaxTextures;

    std::vector<nvrhi::TextureHandle> mVirtualTextures;
    std::unordered_map<nvrhi::ITexture *, uint32_t> mTextureToVirtualID;

    nvrhi::BindingSetDesc mBindingSetDesc;
    nvrhi::BindingSetHandle mCurrentBindingSet;

    bool mIsDirty = true;
};

struct TriangleBasedRenderingCommandList {
    std::vector<TriangleBasedInstanceRenderingData> Instances;

    void Clear() {
        Instances.clear();
    }

    void AddTriangle(const TriangleBasedInstanceRenderingData::VertexPosition &v0,
                     const TriangleBasedInstanceRenderingData::VertexPosition &v1,
                     const TriangleBasedInstanceRenderingData::VertexPosition &v2,
                     int virtualTextureID,
                     uint32_t tintColor,
                     int depth
    ) {
        Instances.push_back(
            TriangleBasedInstanceRenderingData::Triangle(v0, v1, v2, virtualTextureID, tintColor, depth));
    }

    void AddQuad(const TriangleBasedInstanceRenderingData::VertexPosition &v0,
                 const TriangleBasedInstanceRenderingData::VertexPosition &v1,
                 const TriangleBasedInstanceRenderingData::VertexPosition &v2,
                 const TriangleBasedInstanceRenderingData::VertexPosition &v3,
                 int virtualTextureID,
                 uint32_t tintColor,
                 int depth
    ) {
        Instances.push_back(
            TriangleBasedInstanceRenderingData::Quad(v0, v1, v2, v3, virtualTextureID, tintColor, depth));
    }


    std::vector<TriangleRendererSubmissionData> RecordRendererSubmissionData(
        size_t triangleBufferInstanceSizeMax) {
        auto now = std::chrono::high_resolution_clock::now();
        // stable sort by depth, then by texture ID to minimize texture switches
        std::ranges::stable_sort(Instances, [](const auto &a, const auto &b) {
            if (a.Depth != b.Depth) return a.Depth < b.Depth;
            return a.VirtualTextureID < b.VirtualTextureID;
        });
        auto sortEnd = std::chrono::high_resolution_clock::now();

        std::vector<TriangleRendererSubmissionData> submissions;
        if (Instances.empty()) return submissions;

        auto lastFrameSubmissionIt = mLastFrameCache.begin();

        TriangleRendererSubmissionData currentSubmission;
        if (lastFrameSubmissionIt != mLastFrameCache.end()) {
            currentSubmission = std::move(*lastFrameSubmissionIt);
            currentSubmission.VertexData.clear();
            currentSubmission.IndexData.clear();
            currentSubmission.InstanceData.clear();
            ++lastFrameSubmissionIt;
        }

        auto finalizeSubmission = [&]() mutable {
            if (!currentSubmission.VertexData.empty()) {
                submissions.push_back(std::move(currentSubmission));

                if (lastFrameSubmissionIt == mLastFrameCache.end()) {
                    currentSubmission.Clear();
                } else {
                    currentSubmission = std::move(*lastFrameSubmissionIt);
                    currentSubmission.VertexData.clear();
                    currentSubmission.IndexData.clear();
                    currentSubmission.InstanceData.clear();
                    ++lastFrameSubmissionIt;
                }
            }
        };

        for (const auto &instance: Instances) {
            // check if we need to finalize due to vertex/index buffer size
            if (currentSubmission.InstanceData.size() + 1 >
                triangleBufferInstanceSizeMax) {
                finalizeSubmission();
            }

            int32_t finalTextureIndex = instance.VirtualTextureID;

            // Fill Instance Data
            auto instanceIndex = static_cast<uint32_t>(currentSubmission.InstanceData.size());

            currentSubmission.InstanceData.reserve(currentSubmission.InstanceData.size());

            currentSubmission.InstanceData.push_back({
                .tintColor = instance.TintColor,
                .textureIndex = finalTextureIndex
            });

            uint32_t baseVtx = static_cast<uint32_t>(currentSubmission.VertexData.size());

            if (!instance.IsQuad) {
                currentSubmission.VertexData.resize(currentSubmission.VertexData.size() + 3);
                for (int i = 0; i < 3; ++i) {
                    TriangleVertexData *v = &currentSubmission.VertexData[baseVtx + i];
                    v->position = {instance.Vertices[i].x, instance.Vertices[i].y};
                    v->texCoords = {instance.Vertices[i].u, instance.Vertices[i].v};
                    v->constantIndex = instanceIndex;
                }
                currentSubmission.IndexData.resize(currentSubmission.IndexData.size() + 3);
                uint32_t *idx0 = &currentSubmission.IndexData[currentSubmission.IndexData.size() - 3];
                idx0[0] = baseVtx + 0;
                idx0[1] = baseVtx + 1;
                idx0[2] = baseVtx + 2;
            } else {
                // Quad (Assume TL, TR, BR, BL)
                currentSubmission.VertexData.resize(currentSubmission.VertexData.size() + 4);
                for (int i = 0; i < 4; ++i) {
                    TriangleVertexData *v = &currentSubmission.VertexData[baseVtx + i];
                    v->position = {instance.Vertices[i].x, instance.Vertices[i].y};
                    v->texCoords = {instance.Vertices[i].u, instance.Vertices[i].v};
                    v->constantIndex = instanceIndex;
                }
                currentSubmission.IndexData.resize(currentSubmission.IndexData.size() + 6);
                uint32_t *idx0 = &currentSubmission.IndexData[currentSubmission.IndexData.size() - 6];
                idx0[0] = baseVtx + 0;
                idx0[1] = baseVtx + 1;
                idx0[2] = baseVtx + 2;
                idx0[3] = baseVtx + 0;
                idx0[4] = baseVtx + 2;
                idx0[5] = baseVtx + 3;
            }
        }

        finalizeSubmission();

        auto recordEnd = std::chrono::high_resolution_clock::now();
        ImGui::Text("Sorting Time: %.3f ms",
                    std::chrono::duration<float, std::milli>(sortEnd - now).count());
        ImGui::Text("Recording Time: %.3f ms",
                    std::chrono::duration<float, std::milli>(recordEnd - sortEnd).count());

        return submissions;
    }

private:
    std::vector<TriangleRendererSubmissionData> mLastFrameCache;

public:
    void GiveBackForNextFrame(std::vector<TriangleRendererSubmissionData> &&thisCache) {
        mLastFrameCache = std::move(thisCache);
        mLastFrameCache.resize(0);
    }
};

struct BatchRenderingResources {
    nvrhi::BufferHandle VertexBuffer;
    nvrhi::BufferHandle IndexBuffer;
    nvrhi::BufferHandle InstanceBuffer;
    nvrhi::BindingSetHandle mBindingSetSpace0;
};


class Renderer2D {
public:
    Renderer2D(nvrhi::IDevice *device, uint32_t width, uint32_t height)
        : mDevice(device), mWidth(width), mHeight(height), mVirtualTextureManager(device) {
        CreateResources();
        CreateConstantBuffers();
        CreatePipelines();
    }

    void BeginRendering();

    void EndRendering();

    void OnResize(uint32_t width, uint32_t height);

    nvrhi::ITexture *GetTexture() const {
        return mTexture.Get();
    }

    void Clear() {
        mTriangleCommandList.Clear();
    }

    [[nodiscard]] int GetCurrentDepth() const { return mCurrentDepth; }
    void SetCurrentDepth(int depth) { mCurrentDepth = depth; }

    uint32_t RegisterVirtualTextureForThisFrame(const nvrhi::TextureHandle &texture) {
        return mVirtualTextureManager.RegisterTexture(texture);
    }

private:
    static uint32_t ToRGBAUInt32(const nvrhi::Color &color);

private:
    nvrhi::DeviceHandle mDevice;
    uint32_t mWidth, mHeight;

    nvrhi::TextureHandle mTexture;
    nvrhi::FramebufferHandle mFramebuffer;

    PersistentVirtualTextureManager mVirtualTextureManager;

    size_t mBindlessTextureArraySizeMax{};

    nvrhi::CommandListHandle mCommandList;
    nvrhi::SamplerHandle mTextureSampler;

    std::array<float, 16> mViewProjectionMatrix;

    int mCurrentDepth = 0;

    TriangleBasedRenderingCommandList mTriangleCommandList;
    nvrhi::InputLayoutHandle mTriangleInputLayout;
    nvrhi::GraphicsPipelineHandle mTrianglePipeline;
    std::array<nvrhi::BindingLayoutHandle, 2> mTriangleBindingLayouts;
    nvrhi::BufferHandle mTriangleConstantBuffer;
    size_t mTriangleBufferInstanceSizeMax;
    std::vector<BatchRenderingResources> mTriangleBatchRenderingResources;

    void CreateResources();

    void CreateTriangleBatchRenderingResources(size_t count);

    void CreatePipelines();

    void CreateConstantBuffers();

    void CreatePipelineTriangle();

    void CreateConstantBufferTriangle();

    void SubmitTriangleBatchRendering();

    void Submit();

public:
    void DrawTriangleColored(const std::array<float, 2> &v0,
                             const std::array<float, 2> &v1,
                             const std::array<float, 2> &v2,
                             nvrhi::Color tintColor,
                             std::optional<int> overrideDepth = std::nullopt);

    void DrawTriangleTextureVirtual(const std::array<float, 2> &v0,
                                    const std::array<float, 2> &v1,
                                    const std::array<float, 2> &v2,
                                    const std::array<float, 2> &uv0,
                                    const std::array<float, 2> &uv1,
                                    const std::array<float, 2> &uv2,
                                    uint32_t virtualTextureID,
                                    std::optional<int> overrideDepth = std::nullopt,
                                    nvrhi::Color tintColor = nvrhi::Color(1.f, 1.f, 1.f, 1.f));

    uint32_t DrawTriangleTextureManaged(const std::array<float, 2> &v0,
                                        const std::array<float, 2> &v1,
                                        const std::array<float, 2> &v2,
                                        const std::array<float, 2> &uv0,
                                        const std::array<float, 2> &uv1,
                                        const std::array<float, 2> &uv2,
                                        const nvrhi::TextureHandle &texture,
                                        std::optional<int> overrideDepth = std::nullopt,
                                        nvrhi::Color tintColor = nvrhi::Color(1.f, 1.f, 1.f, 1.f));

    void DrawQuadColored(const std::array<float, 2> &v0,
                         const std::array<float, 2> &v1,
                         const std::array<float, 2> &v2,
                         const std::array<float, 2> &v3,
                         nvrhi::Color tintColor,
                         std::optional<int> overrideDepth = std::nullopt
    );

    void DrawQuadTextureVirtual(const std::array<float, 2> &v0,
                                const std::array<float, 2> &v1,
                                const std::array<float, 2> &v2,
                                const std::array<float, 2> &v3,
                                const std::array<float, 2> &uv0,
                                const std::array<float, 2> &uv1,
                                const std::array<float, 2> &uv2,
                                const std::array<float, 2> &uv3,
                                uint32_t virtualTextureID,
                                std::optional<int> overrideDepth = std::nullopt,
                                nvrhi::Color tintColor = nvrhi::Color(1.f, 1.f, 1.f, 1.f));

    uint32_t DrawQuadTextureManaged(const std::array<float, 2> &v0,
                                    const std::array<float, 2> &v1,
                                    const std::array<float, 2> &v2,
                                    const std::array<float, 2> &v3,
                                    const std::array<float, 2> &uv0,
                                    const std::array<float, 2> &uv1,
                                    const std::array<float, 2> &uv2,
                                    const std::array<float, 2> &uv3,
                                    const nvrhi::TextureHandle &texture,
                                    std::optional<int> overrideDepth = std::nullopt,
                                    nvrhi::Color tintColor = nvrhi::Color(1.f, 1.f, 1.f, 1.f));
};

void Renderer2D::BeginRendering() {
    Clear();

    mCommandList->open();

    mCommandList->setResourceStatesForFramebuffer(mFramebuffer);
    mCommandList->clearTextureFloat(mTexture,
                                    nvrhi::AllSubresources, nvrhi::Color(0.f, 0.f, 0.f, 0.f));
}

void Renderer2D::EndRendering() {
    Submit();
    mCommandList->close();
    // auto now = std::chrono::high_resolution_clock::now();
    // nvrhi::EventQueryHandle eventQuery = mDevice->createEventQuery();
    mDevice->executeCommandList(mCommandList);
    // mDevice->setEventQuery(eventQuery, nvrhi::CommandQueue::Graphics);
    // mDevice->waitEventQuery(eventQuery);
    // auto end = std::chrono::high_resolution_clock::now();
    // ImGui::Text("GPU Submission Time: %.3f ms",
    // std::chrono::duration<float, std::milli>(end - now).count());

    if (mVirtualTextureManager.IsSubOptimal()) {
        mVirtualTextureManager.Optimize();
    }
}

void Renderer2D::OnResize(uint32_t width, uint32_t height) {
    if (width == mWidth && height == mHeight) return;
    mDevice->waitForIdle();

    mWidth = width;
    mHeight = height;
    mTexture.Reset();
    mFramebuffer.Reset();

    CreateResources();

    mViewProjectionMatrix = {
        2.0f / static_cast<float>(mWidth), 0.0f, 0.0f, 0.0f,
        0.0f, -2.0f / static_cast<float>(mHeight), 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        -1.0f, 1.0f, 0.0f, 1.0f
    };
}

uint32_t Renderer2D::ToRGBAUInt32(const nvrhi::Color &color) {
    auto r = static_cast<uint32_t>(std::clamp(color.r * 255.f, 0.f, 255.f));
    auto g = static_cast<uint32_t>(std::clamp(color.g * 255.f, 0.f, 255.f));
    auto b = static_cast<uint32_t>(std::clamp(color.b * 255.f, 0.f, 255.f));
    auto a = static_cast<uint32_t>(std::clamp(color.a * 255.f, 0.f, 255.f));
    return (r << 24) | (g << 16) | (b << 8) | a;
}

void Renderer2D::DrawTriangleColored(const std::array<float, 2> &v0, const std::array<float, 2> &v1,
                                     const std::array<float, 2> &v2, nvrhi::Color tintColor,
                                     std::optional<int> overrideDepth) {
    TriangleBasedInstanceRenderingData::VertexPosition vert0{v0[0], v0[1], 0.f, 0.f};
    TriangleBasedInstanceRenderingData::VertexPosition vert1{v1[0], v1[1], 0.f, 0.f};
    TriangleBasedInstanceRenderingData::VertexPosition vert2{v2[0], v2[1], 0.f, 0.f};
    mTriangleCommandList.AddTriangle(vert0, vert1, vert2, -1, ToRGBAUInt32(tintColor),
                                     overrideDepth.has_value() ? overrideDepth.value() : mCurrentDepth);
}

void Renderer2D::DrawTriangleTextureVirtual(const std::array<float, 2> &v0, const std::array<float, 2> &v1,
                                            const std::array<float, 2> &v2, const std::array<float, 2> &uv0,
                                            const std::array<float, 2> &uv1,
                                            const std::array<float, 2> &uv2, uint32_t virtualTextureID,
                                            std::optional<int> overrideDepth,
                                            nvrhi::Color tintColor) {
    TriangleBasedInstanceRenderingData::VertexPosition vert0{v0[0], v0[1], uv0[0], uv0[1]};
    TriangleBasedInstanceRenderingData::VertexPosition vert1{v1[0], v1[1], uv1[0], uv1[1]};
    TriangleBasedInstanceRenderingData::VertexPosition vert2{v2[0], v2[1], uv2[0], uv2[1]};
    mTriangleCommandList.AddTriangle(vert0, vert1, vert2, virtualTextureID, ToRGBAUInt32(tintColor),
                                     overrideDepth.has_value() ? overrideDepth.value() : mCurrentDepth);
}

uint32_t Renderer2D::DrawTriangleTextureManaged(const std::array<float, 2> &v0, const std::array<float, 2> &v1,
                                                const std::array<float, 2> &v2, const std::array<float, 2> &uv0,
                                                const std::array<float, 2> &uv1,
                                                const std::array<float, 2> &uv2, const nvrhi::TextureHandle &texture,
                                                std::optional<int> overrideDepth,
                                                nvrhi::Color tintColor) {
    uint32_t virtualTextureID = mVirtualTextureManager.RegisterTexture(texture);
    DrawTriangleTextureVirtual(v0, v1, v2, uv0, uv1, uv2, virtualTextureID, overrideDepth, tintColor);
    return virtualTextureID;
}

void Renderer2D::DrawQuadColored(const std::array<float, 2> &v0, const std::array<float, 2> &v1,
                                 const std::array<float, 2> &v2, const std::array<float, 2> &v3,
                                 nvrhi::Color tintColor, std::optional<int> overrideDepth) {
    TriangleBasedInstanceRenderingData::VertexPosition vert0{v0[0], v0[1], 0.f, 0.f};
    TriangleBasedInstanceRenderingData::VertexPosition vert1{v1[0], v1[1], 0.f, 0.f};
    TriangleBasedInstanceRenderingData::VertexPosition vert2{v2[0], v2[1], 0.f, 0.f};
    TriangleBasedInstanceRenderingData::VertexPosition vert3{v3[0], v3[1], 0.f, 0.f};
    mTriangleCommandList.AddQuad(vert0, vert1, vert2, vert3, -1, ToRGBAUInt32(tintColor),
                                 overrideDepth.has_value() ? overrideDepth.value() : mCurrentDepth);
}

void Renderer2D::DrawQuadTextureVirtual(const std::array<float, 2> &v0, const std::array<float, 2> &v1,
                                        const std::array<float, 2> &v2, const std::array<float, 2> &v3,
                                        const std::array<float, 2> &uv0,
                                        const std::array<float, 2> &uv1, const std::array<float, 2> &uv2,
                                        const std::array<float, 2> &uv3,
                                        uint32_t virtualTextureID,
                                        std::optional<int> overrideDepth,
                                        nvrhi::Color tintColor) {
    TriangleBasedInstanceRenderingData::VertexPosition vert0{v0[0], v0[1], uv0[0], uv0[1]};
    TriangleBasedInstanceRenderingData::VertexPosition vert1{v1[0], v1[1], uv1[0], uv1[1]};
    TriangleBasedInstanceRenderingData::VertexPosition vert2{v2[0], v2[1], uv2[0], uv2[1]};
    TriangleBasedInstanceRenderingData::VertexPosition vert3{v3[0], v3[1], uv3[0], uv3[1]};
    mTriangleCommandList.AddQuad(vert0, vert1, vert2, vert3, virtualTextureID, ToRGBAUInt32(tintColor),
                                 overrideDepth.has_value() ? overrideDepth.value() : mCurrentDepth);
}

uint32_t Renderer2D::DrawQuadTextureManaged(const std::array<float, 2> &v0, const std::array<float, 2> &v1,
                                            const std::array<float, 2> &v2, const std::array<float, 2> &v3,
                                            const std::array<float, 2> &uv0,
                                            const std::array<float, 2> &uv1, const std::array<float, 2> &uv2,
                                            const std::array<float, 2> &uv3,
                                            const nvrhi::TextureHandle &texture,
                                            std::optional<int> overrideDepth,
                                            nvrhi::Color tintColor) {
    uint32_t virtualTextureID = mVirtualTextureManager.RegisterTexture(texture);
    DrawQuadTextureVirtual(v0, v1, v2, v3, uv0, uv1, uv2, uv3, virtualTextureID, overrideDepth, tintColor);
    return virtualTextureID;
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

    auto tex = mDevice->createTexture(texDesc);
    mTexture = tex;
    mFramebuffer = mDevice->createFramebuffer(
        nvrhi::FramebufferDesc().addColorAttachment(tex));

    if (!mCommandList) {
        mCommandList = mDevice->createCommandList();
    }

    mTextureSampler = mDevice->createSampler(nvrhi::SamplerDesc()
        .setAllAddressModes(nvrhi::SamplerAddressMode::Clamp)
        .setAllFilters(true));

    vk::PhysicalDevice vkPhysicalDevice = static_cast<vk::PhysicalDevice>(
        mDevice->getNativeObject(nvrhi::ObjectTypes::VK_PhysicalDevice)
    );

    vk::PhysicalDeviceProperties deviceProperties = vkPhysicalDevice.getProperties();

    uint32_t hardwareMax = deviceProperties.limits.maxDescriptorSetSampledImages;

    mBindlessTextureArraySizeMax = std::min<uint32_t>(16384u, hardwareMax);
    mTriangleBufferInstanceSizeMax = 1 << 18; // 2^18 instances

    mViewProjectionMatrix = {
        2.0f / static_cast<float>(mWidth), 0.0f, 0.0f, 0.0f,
        0.0f, -2.0f / static_cast<float>(mHeight), 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        -1.0f, 1.0f, 0.0f, 1.0f
    };
}

void Renderer2D::CreateTriangleBatchRenderingResources(size_t count) {
    if (count <= mTriangleBatchRenderingResources.size()) {
        return;
    }

    for (size_t i = mTriangleBatchRenderingResources.size(); i < count; ++i) {
        BatchRenderingResources resources;

        nvrhi::BufferDesc vertexBufferDesc;
        vertexBufferDesc.byteSize = sizeof(TriangleVertexData) * mTriangleBufferInstanceSizeMax * 4;
        vertexBufferDesc.isVertexBuffer = true;
        vertexBufferDesc.debugName = "Renderer2D::TriangleVertexBuffer";
        vertexBufferDesc.initialState = nvrhi::ResourceStates::VertexBuffer;
        vertexBufferDesc.keepInitialState = true;
        resources.VertexBuffer = mDevice->createBuffer(vertexBufferDesc);

        nvrhi::BufferDesc indexBufferDesc;
        indexBufferDesc.byteSize = sizeof(uint32_t) * mTriangleBufferInstanceSizeMax * 6;
        indexBufferDesc.isIndexBuffer = true;
        indexBufferDesc.debugName = "Renderer2D::TriangleIndexBuffer";
        indexBufferDesc.initialState = nvrhi::ResourceStates::IndexBuffer;
        indexBufferDesc.keepInitialState = true;
        resources.IndexBuffer = mDevice->createBuffer(indexBufferDesc);

        nvrhi::BufferDesc instanceBufferDesc;
        instanceBufferDesc.byteSize = sizeof(SpriteData) * mTriangleBufferInstanceSizeMax;
        instanceBufferDesc.canHaveRawViews = true;
        instanceBufferDesc.structStride = sizeof(SpriteData);
        instanceBufferDesc.debugName = "Renderer2D::TriangleInstanceBuffer";
        instanceBufferDesc.initialState = nvrhi::ResourceStates::ShaderResource;
        instanceBufferDesc.keepInitialState = true;
        resources.InstanceBuffer = mDevice->createBuffer(instanceBufferDesc);

        nvrhi::BindingSetDesc bindingSetDesc;
        bindingSetDesc.addItem(nvrhi::BindingSetItem::ConstantBuffer(0, mTriangleConstantBuffer));
        bindingSetDesc.addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(0, resources.InstanceBuffer));
        bindingSetDesc.addItem(nvrhi::BindingSetItem::Sampler(0, mTextureSampler));
        resources.mBindingSetSpace0 = mDevice->createBindingSet(bindingSetDesc, mTriangleBindingLayouts[0]);

        mTriangleBatchRenderingResources.push_back(resources);
    }
}

void Renderer2D::CreatePipelines() {
    CreatePipelineTriangle();
}

void Renderer2D::CreateConstantBuffers() {
    CreateConstantBufferTriangle();
}

void Renderer2D::CreatePipelineTriangle() {
    nvrhi::ShaderDesc vsDesc;
    vsDesc.shaderType = nvrhi::ShaderType::Vertex;
    vsDesc.entryName = "main";
    nvrhi::ShaderHandle vs = mDevice->createShader(vsDesc,
                                                   GeneratedShaders::renderer2d_triangle_vs.data(),
                                                   GeneratedShaders::renderer2d_triangle_vs.size());

    nvrhi::ShaderDesc psDesc;
    psDesc.shaderType = nvrhi::ShaderType::Pixel;
    psDesc.entryName = "main";
    nvrhi::ShaderHandle ps = mDevice->createShader(psDesc,
                                                   GeneratedShaders::renderer2d_triangle_ps.data(),
                                                   GeneratedShaders::renderer2d_triangle_ps.size());

    nvrhi::VertexAttributeDesc posAttrs[3];
    posAttrs[0].name = "POSITION";
    posAttrs[0].format = nvrhi::Format::RG32_FLOAT;
    posAttrs[0].bufferIndex = 0;
    posAttrs[0].offset = offsetof(TriangleVertexData, position);
    posAttrs[0].elementStride = sizeof(TriangleVertexData);

    posAttrs[1].name = "TEXCOORD";
    posAttrs[1].format = nvrhi::Format::RG32_FLOAT;
    posAttrs[1].bufferIndex = 0;
    posAttrs[1].offset = offsetof(TriangleVertexData, texCoords);
    posAttrs[1].elementStride = sizeof(TriangleVertexData);

    posAttrs[2].name = "CONSTANTINDEX";
    posAttrs[2].format = nvrhi::Format::R32_UINT;
    posAttrs[2].bufferIndex = 0;
    posAttrs[2].offset = offsetof(TriangleVertexData, constantIndex);
    posAttrs[2].elementStride = sizeof(TriangleVertexData);

    mTriangleInputLayout = mDevice->createInputLayout(posAttrs, 3, vs);

    nvrhi::BindingLayoutDesc bindingLayoutDesc[2];
    bindingLayoutDesc[0].visibility = nvrhi::ShaderType::Vertex | nvrhi::ShaderType::Pixel;
    bindingLayoutDesc[0].bindings = {
        nvrhi::BindingLayoutItem::ConstantBuffer(0),
        nvrhi::BindingLayoutItem::StructuredBuffer_SRV(0),
        nvrhi::BindingLayoutItem::Sampler(0)
    };

    bindingLayoutDesc[1].visibility = nvrhi::ShaderType::Pixel;
    bindingLayoutDesc[1].bindings = {
        nvrhi::BindingLayoutItem::Texture_SRV(0).setSize(mBindlessTextureArraySizeMax)
    };

    mTriangleBindingLayouts[0] = mDevice->createBindingLayout(bindingLayoutDesc[0]);
    mTriangleBindingLayouts[1] = mDevice->createBindingLayout(bindingLayoutDesc[1]);

    nvrhi::GraphicsPipelineDesc pipeDesc;
    pipeDesc.VS = vs;
    pipeDesc.PS = ps;
    pipeDesc.inputLayout = mTriangleInputLayout;
    pipeDesc.bindingLayouts = {
        mTriangleBindingLayouts[0],
        mTriangleBindingLayouts[1]
    };

    pipeDesc.primType = nvrhi::PrimitiveType::TriangleList;

    pipeDesc.renderState.blendState.targets[0].blendEnable = true;
    pipeDesc.renderState.blendState.targets[0].srcBlend = nvrhi::BlendFactor::SrcAlpha;
    pipeDesc.renderState.blendState.targets[0].destBlend = nvrhi::BlendFactor::InvSrcAlpha;
    pipeDesc.renderState.blendState.targets[0].srcBlendAlpha = nvrhi::BlendFactor::One;
    pipeDesc.renderState.blendState.targets[0].destBlendAlpha = nvrhi::BlendFactor::InvSrcAlpha;
    pipeDesc.renderState.blendState.targets[0].colorWriteMask = nvrhi::ColorMask::All;
    pipeDesc.renderState.rasterState.cullMode = nvrhi::RasterCullMode::None;
    pipeDesc.renderState.depthStencilState.depthTestEnable = false;

    mTrianglePipeline = mDevice->createGraphicsPipeline(pipeDesc, mFramebuffer->getFramebufferInfo());

    CreateTriangleBatchRenderingResources(4); // this should be enough for most cases, if not we can always expand it
}

void Renderer2D::CreateConstantBufferTriangle() {
    nvrhi::BufferDesc constBufferVPMatrixDesc;
    constBufferVPMatrixDesc.byteSize = sizeof(float) * 4 * 4;
    constBufferVPMatrixDesc.isConstantBuffer = true;
    constBufferVPMatrixDesc.debugName = "Renderer2D::ConstantBufferVPMatrix";
    constBufferVPMatrixDesc.initialState = nvrhi::ResourceStates::ShaderResource |
                                           nvrhi::ResourceStates::ConstantBuffer;
    constBufferVPMatrixDesc.keepInitialState = true;
    mTriangleConstantBuffer = mDevice->createBuffer(constBufferVPMatrixDesc);
}

void Renderer2D::SubmitTriangleBatchRendering() {
    auto submissions = mTriangleCommandList.RecordRendererSubmissionData(
        mTriangleBufferInstanceSizeMax);

    CreateTriangleBatchRenderingResources(submissions.size());

    // submit constant buffer
    mCommandList->writeBuffer(mTriangleConstantBuffer, mViewProjectionMatrix.data(),
                              sizeof(float) * 16, 0);

    for (size_t i = 0; i < submissions.size(); ++i) {
        auto &submission = submissions[i];
        auto &resources = mTriangleBatchRenderingResources[i];

        // Update Buffers
        if (!submission.VertexData.empty()) {
            mCommandList->writeBuffer(resources.VertexBuffer, submission.VertexData.data(),
                                      sizeof(TriangleVertexData) * submission.VertexData.size(), 0);
        }

        if (!submission.IndexData.empty()) {
            mCommandList->writeBuffer(resources.IndexBuffer, submission.IndexData.data(),
                                      sizeof(uint32_t) * submission.IndexData.size(), 0);
        }

        if (!submission.InstanceData.empty()) {
            mCommandList->writeBuffer(resources.InstanceBuffer, submission.InstanceData.data(),
                                      sizeof(SpriteData) * submission.InstanceData.size(), 0);
        }


        mCommandList->setResourceStatesForBindingSet(resources.mBindingSetSpace0);
        auto bindingSetSpace1 = mVirtualTextureManager.GetBindingSet(mTriangleBindingLayouts[1]);
        mCommandList->setResourceStatesForBindingSet(bindingSetSpace1);


        // Draw Call
        nvrhi::GraphicsState state;
        state.pipeline = mTrianglePipeline;
        state.framebuffer = mFramebuffer;
        state.viewport.addViewportAndScissorRect(
            mFramebuffer->getFramebufferInfo().getViewport());
        state.bindings.push_back(resources.mBindingSetSpace0);
        state.bindings.push_back(bindingSetSpace1);

        nvrhi::VertexBufferBinding vertexBufferBinding;
        vertexBufferBinding.buffer = resources.VertexBuffer;
        vertexBufferBinding.offset = 0;
        vertexBufferBinding.slot = 0;

        state.vertexBuffers.push_back(vertexBufferBinding);

        nvrhi::IndexBufferBinding indexBufferBinding;
        indexBufferBinding.buffer = resources.IndexBuffer;
        indexBufferBinding.format = nvrhi::Format::R32_UINT;
        indexBufferBinding.offset = 0;

        state.indexBuffer = indexBufferBinding;

        mCommandList->setGraphicsState(state);

        nvrhi::DrawArguments drawArgs;
        drawArgs.vertexCount = static_cast<uint32_t>(submission.IndexData.size());

        mCommandList->drawIndexed(drawArgs);
    }

    mTriangleCommandList.GiveBackForNextFrame(std::move(submissions));
}

void Renderer2D::Submit() {
    SubmitTriangleBatchRendering();
}

export class RendererDevelopmentLayer : public Layer {
public:
    virtual void OnAttach(const std::shared_ptr<Application> &app) override {
        Layer::OnAttach(app);
        auto &swapchain = mApp->GetSwapchainData();

        mRenderer = std::make_shared<Renderer2D>(mApp->GetNvrhiDevice().Get(),
                                                 swapchain.GetWidth(),
                                                 swapchain.GetHeight());

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
    }

    virtual void OnRender(const nvrhi::CommandListHandle &commandList,
                          const nvrhi::FramebufferHandle &framebuffer, uint32_t frameIndex) override {
        ImGui::Begin("TriangleBasedRenderingCommandList Profiling");

        mRenderer->BeginRendering();

        const int quadCountX = 250;
        const int quadCountY = 150;

        const float quadSize = 8.0f;
        const float spacing = 2.0f;

        uint32_t texIdRed = mRenderer->RegisterVirtualTextureForThisFrame(mRedTextureHandle);
        uint32_t texIdGreen = mRenderer->RegisterVirtualTextureForThisFrame(mGreenTextureHandle);
        uint32_t texIdBlue = mRenderer->RegisterVirtualTextureForThisFrame(mBlueTextureHandle);

        for (int y = 0; y < quadCountY; ++y) {
            for (int x = 0; x < quadCountX; ++x) {
                float px = 50.0f + x * (quadSize + spacing);
                float py = 50.0f + y * (quadSize + spacing);

                mRenderer->SetCurrentDepth((x + y) & 1);

                uint32_t texId;
                switch ((x + y) % 3) {
                    case 0: texId = texIdRed;
                        break;
                    case 1: texId = texIdGreen;
                        break;
                    default: texId = texIdBlue;
                        break;
                }

                mRenderer->DrawQuadTextureVirtual(
                    {px, py},
                    {px + quadSize, py},
                    {px + quadSize, py + quadSize},
                    {px, py + quadSize},
                    {0.f, 0.f}, {1.f, 0.f}, {1.f, 1.f}, {0.f, 1.f},
                    texId
                );
            }
        }

        mRenderer->EndRendering();

        // Present the renderer's output to the main framebuffer
        mPresenter->Present(commandList, mRenderer->GetTexture(), framebuffer);

        ImGui::End();
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
};
