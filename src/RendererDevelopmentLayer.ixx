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

import glm;
import "glm/gtx/transform.hpp";

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

struct Renderer2DDescriptor {
    glm::u32vec2 OutputSize;
    glm::vec2 VirtualSize;
    nvrhi::DeviceHandle Device;
};

#pragma region TriangleInplementationLogic

export struct TriangleVertexData {
    glm::vec2 position;
    glm::vec2 texCoords;
    uint32_t constantIndex;
};

export struct TriangleInstanceData {
    uint32_t tintColor;
    int32_t textureIndex; // if < 0, no texture
};

export struct TriangleRenderingData {
    struct VertexPosition {
        glm::vec2 position;
        glm::vec2 texCoords;
    };

    glm::mat4x2 Positions; // 4 vertices, each with 2 components (x, y)
    glm::mat4x2 TexCoords; // 4 texture coordinates, each with 2 components (u, v)
    bool IsQuad;
    int VirtualTextureID;
    uint32_t TintColor;
    int Depth;

    static TriangleRenderingData Triangle(const glm::vec2 &p0, const glm::vec2 &uv0,
                                          const glm::vec2 &p1, const glm::vec2 &uv1,
                                          const glm::vec2 &p2, const glm::vec2 &uv2,
                                          int textureIndex,
                                          uint32_t tintColor, int depth = 0) {
        TriangleRenderingData data;
        data.Positions[0] = p0;
        data.Positions[1] = p1;
        data.Positions[2] = p2;
        data.TexCoords[0] = uv0;
        data.TexCoords[1] = uv1;
        data.TexCoords[2] = uv2;
        data.IsQuad = false;
        data.VirtualTextureID = textureIndex;
        data.TintColor = tintColor;
        data.Depth = depth;
        return data;
    }

    static TriangleRenderingData Quad(const glm::vec2 &p0, const glm::vec2 &uv0,
                                      const glm::vec2 &p1, const glm::vec2 &uv1,
                                      const glm::vec2 &p2, const glm::vec2 &uv2,
                                      const glm::vec2 &p3, const glm::vec2 &uv3,
                                      int virtualTextureID,
                                      uint32_t tintColor, int depth = 0) {
        TriangleRenderingData data;
        data.Positions[0] = p0;
        data.Positions[1] = p1;
        data.Positions[2] = p2;
        data.Positions[3] = p3;
        data.TexCoords[0] = uv0;
        data.TexCoords[1] = uv1;
        data.TexCoords[2] = uv2;
        data.TexCoords[3] = uv3;
        data.IsQuad = true;
        data.VirtualTextureID = virtualTextureID;
        data.TintColor = tintColor;
        data.Depth = depth;
        return data;
    }
};

static_assert(std::is_trivially_move_assignable_v<TriangleRenderingData>,
              "TriangleBasedInstanceRenderingData must be trivially move assignable");
static_assert(std::is_trivially_destructible_v<TriangleRenderingData>,
              "TriangleBasedInstanceRenderingData must be trivially destructible");


struct TriangleRenderingSubmissionData {
    std::vector<TriangleVertexData> VertexData;
    std::vector<uint32_t> IndexData;
    std::vector<TriangleInstanceData> InstanceData;

    TriangleRenderingSubmissionData() = default;

    TriangleRenderingSubmissionData(TriangleRenderingSubmissionData &&) = default;

    TriangleRenderingSubmissionData &operator=(TriangleRenderingSubmissionData &&) = default;

    void Clear() {
        VertexData.clear();
        IndexData.clear();
        InstanceData.clear();
    }
};

export struct TriangleRenderingCommandList {
    std::vector<TriangleRenderingData> Instances;

    void Clear() {
        Instances.clear();
    }

    void AddTriangle(const glm::vec2 &p0, const glm::vec2 &uv0,
                     const glm::vec2 &p1, const glm::vec2 &uv1,
                     const glm::vec2 &p2, const glm::vec2 &uv2,
                     int virtualTextureID,
                     uint32_t tintColor,
                     int depth) {
        Instances.resize(Instances.size() + 1);
        Instances.back() = TriangleRenderingData::Triangle(
            p0, uv0, p1, uv1, p2, uv2, virtualTextureID, tintColor, depth);
    }

    void AddQuad(const glm::vec2 &p0, const glm::vec2 &uv0,
                 const glm::vec2 &p1, const glm::vec2 &uv1,
                 const glm::vec2 &p2, const glm::vec2 &uv2,
                 const glm::vec2 &p3, const glm::vec2 &uv3,
                 int virtualTextureID,
                 uint32_t tintColor,
                 int depth) {
        Instances.resize(Instances.size() + 1);
        Instances.back() = TriangleRenderingData::Quad(
            p0, uv0, p1, uv1, p2, uv2, p3, uv3, virtualTextureID, tintColor, depth);
    }

    std::vector<TriangleRenderingSubmissionData> RecordRendererSubmissionData(
        size_t triangleBufferInstanceSizeMax) {
        auto now = std::chrono::high_resolution_clock::now();
        std::ranges::sort(Instances, [](const auto &a, const auto &b) {
            if (a.Depth != b.Depth) return a.Depth < b.Depth;
            return a.VirtualTextureID < b.VirtualTextureID;
        });
        auto sortEnd = std::chrono::high_resolution_clock::now();

        std::vector<TriangleRenderingSubmissionData> submissions;
        if (Instances.empty()) return submissions;

        auto lastFrameSubmissionIt = mLastFrameCache.begin();

        TriangleRenderingSubmissionData currentSubmission;
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
                    v->position = instance.Positions[i];
                    v->texCoords = instance.TexCoords[i];
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
                    v->position = instance.Positions[i];
                    v->texCoords = instance.TexCoords[i];
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
        ImGui::Text("Frame rate: %.2f FPS", ImGui::GetIO().Framerate);
        return submissions;
    }

private:
    std::vector<TriangleRenderingSubmissionData> mLastFrameCache;

public:
    void GiveBackForNextFrame(std::vector<TriangleRenderingSubmissionData> &&thisCache) {
        mLastFrameCache = std::move(thisCache);
        mLastFrameCache.resize(0);
    }
};

struct TriangleBatchRenderingResources {
    nvrhi::BufferHandle VertexBuffer;
    nvrhi::BufferHandle IndexBuffer;
    nvrhi::BufferHandle InstanceBuffer;
    nvrhi::BindingSetHandle mBindingSetSpace0;
};

#pragma endregion

#pragma region LineInplementationLogic

struct LineVertexData {
    glm::vec2 position;
    uint32_t color;
};

struct LineRenderingSubmissionData {
    std::vector<LineVertexData> VertexData;

    void Clear() {
        VertexData.clear();
    }
};

struct LineRenderingCommandList {
    std::vector<LineVertexData> VertexData;

    void Clear() {
        VertexData.clear();
    }


    void AddLine(const glm::vec2 &p0, const glm::u8vec4 &color0,
                 const glm::vec2 &p1, const glm::u8vec4 &color1) {
        VertexData.resize(VertexData.size() + 2);
        LineVertexData *v0 = &VertexData[VertexData.size() - 2];
        v0->position = p0;

        v0->color = (color0.r << 24) | (color0.g << 16) | (color0.b << 8) | color0.a;

        LineVertexData *v1 = &VertexData[VertexData.size() - 1];
        v1->position = p1;
        v1->color = (color1.r << 24) | (color1.g << 16) | (color1.b << 8) | color1.a;
    }


    std::vector<LineRenderingSubmissionData> RecordRendererSubmissionData(size_t lineBufferInstanceSizeMax) {
        auto now = std::chrono::high_resolution_clock::now();

        std::vector<LineRenderingSubmissionData> submissions;
        if (VertexData.empty()) return submissions;

        auto lastFrameSubmissionIt = mLastFrameCache.begin();

        LineRenderingSubmissionData currentSubmission;
        if (lastFrameSubmissionIt != mLastFrameCache.end()) {
            currentSubmission = std::move(*lastFrameSubmissionIt);
            currentSubmission.VertexData.clear();
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
                    ++lastFrameSubmissionIt;
                }
            }
        };

        for (const auto &vertex: VertexData) {
            // check if we need to finalize due to vertex buffer size
            if (currentSubmission.VertexData.size() + 1 >
                lineBufferInstanceSizeMax) {
                finalizeSubmission();
            }

            currentSubmission.VertexData.push_back(vertex);
        }

        finalizeSubmission();

        auto recordEnd = std::chrono::high_resolution_clock::now();
        ImGui::Text("Line Count: %zu lines (%zu vertices)", VertexData.size() / 2, VertexData.size());
        ImGui::Text("Line Submissions: %zu", submissions.size());
        ImGui::Text("Line Recording Time: %.3f ms",
                    std::chrono::duration<float, std::milli>(recordEnd - now).count());
        return submissions;
    }

private:
    std::vector<LineRenderingSubmissionData> mLastFrameCache;

public:
    void GiveBackForNextFrame(std::vector<LineRenderingSubmissionData> &&thisCache) {
        mLastFrameCache = std::move(thisCache);
        mLastFrameCache.resize(0);
    }
};

struct LineBatchRenderingResources {
    nvrhi::BufferHandle VertexBuffer;
    nvrhi::BindingSetHandle mBindingSetSpace0;
};

#pragma endregion

#pragma region EllipseImplementationLogic

struct EllipseShapeData {
    glm::vec2 center;
    glm::vec2 radii;
    float rotation;
    float innerScale;
    float startAngle;
    float endAngle;
    uint32_t tintColor;
    int32_t textureIndex;
    float edgeSoftness;
    float _padding;
};

static_assert(sizeof(EllipseShapeData) == 48, "EllipseShapeData must be 48 bytes for GPU alignment");

export struct EllipseRenderingData {
    glm::vec2 center;
    glm::vec2 radii;
    float rotation = 0.0f;
    float innerScale = 0.0f;
    float startAngle = 0.0f;
    float endAngle = 0.0f;
    int virtualTextureID = -1;
    uint32_t tintColor = 0xFFFFFFFF;
    float edgeSoftness = 1.0f;
    int depth = 0;

    static EllipseRenderingData Circle(const glm::vec2& center, float radius,
                                       const glm::u8vec4& color, int depth = 0) {
        EllipseRenderingData data;
        data.center = center;
        data.radii = glm::vec2(radius, radius);
        data.tintColor = (color.r << 24) | (color.g << 16) | (color.b << 8) | color.a;
        data.depth = depth;
        return data;
    }

    static EllipseRenderingData Ellipse(const glm::vec2& center, const glm::vec2& radii,
                                        float rotation, const glm::u8vec4& color, int depth = 0) {
        EllipseRenderingData data;
        data.center = center;
        data.radii = radii;
        data.rotation = rotation;
        data.tintColor = (color.r << 24) | (color.g << 16) | (color.b << 8) | color.a;
        data.depth = depth;
        return data;
    }

    static EllipseRenderingData Ring(const glm::vec2& center, float outerRadius, float innerRadius,
                                     const glm::u8vec4& color, int depth = 0) {
        EllipseRenderingData data;
        data.center = center;
        data.radii = glm::vec2(outerRadius, outerRadius);
        data.innerScale = innerRadius / outerRadius;
        data.tintColor = (color.r << 24) | (color.g << 16) | (color.b << 8) | color.a;
        data.depth = depth;
        return data;
    }

    static EllipseRenderingData Sector(const glm::vec2& center, float radius,
                                       float startAngle, float endAngle,
                                       const glm::u8vec4& color, int textureIndex = -1, int depth = 0) {
        EllipseRenderingData data;
        data.center = center;
        data.radii = glm::vec2(radius, radius);
        data.startAngle = startAngle;
        data.endAngle = endAngle;
        data.virtualTextureID = textureIndex;
        data.tintColor = (color.r << 24) | (color.g << 16) | (color.b << 8) | color.a;
        data.depth = depth;
        return data;
    }

    static EllipseRenderingData Arc(const glm::vec2& center, float radius, float thickness,
                                    float startAngle, float endAngle,
                                    const glm::u8vec4& color, int depth = 0) {
        EllipseRenderingData data;
        data.center = center;
        data.radii = glm::vec2(radius, radius);
        data.innerScale = (radius - thickness) / radius;
        data.startAngle = startAngle;
        data.endAngle = endAngle;
        data.tintColor = (color.r << 24) | (color.g << 16) | (color.b << 8) | color.a;
        data.depth = depth;
        return data;
    }

    static EllipseRenderingData EllipseSector(const glm::vec2& center, const glm::vec2& radii,
                                              float rotation, float startAngle, float endAngle,
                                              const glm::u8vec4& color, int textureIndex = -1, int depth = 0) {
        EllipseRenderingData data;
        data.center = center;
        data.radii = radii;
        data.rotation = rotation;
        data.startAngle = startAngle;
        data.endAngle = endAngle;
        data.virtualTextureID = textureIndex;
        data.tintColor = (color.r << 24) | (color.g << 16) | (color.b << 8) | color.a;
        data.depth = depth;
        return data;
    }

    static EllipseRenderingData EllipseArc(const glm::vec2& center, const glm::vec2& radii,
                                           float rotation, float thickness,
                                           float startAngle, float endAngle,
                                           const glm::u8vec4& color, int depth = 0) {
        EllipseRenderingData data;
        data.center = center;
        data.radii = radii;
        data.rotation = rotation;
        float minRadius = glm::min(radii.x, radii.y);
        data.innerScale = glm::max(0.0f, (minRadius - thickness) / minRadius);
        data.startAngle = startAngle;
        data.endAngle = endAngle;
        data.tintColor = (color.r << 24) | (color.g << 16) | (color.b << 8) | color.a;
        data.depth = depth;
        return data;
    }
};

struct EllipseRenderingSubmissionData {
    std::vector<EllipseShapeData> ShapeData;

    EllipseRenderingSubmissionData() = default;
    EllipseRenderingSubmissionData(EllipseRenderingSubmissionData&&) = default;
    EllipseRenderingSubmissionData& operator=(EllipseRenderingSubmissionData&&) = default;

    void Clear() {
        ShapeData.clear();
    }
};

struct EllipseRenderingCommandList {
    std::vector<EllipseRenderingData> Instances;

    void Clear() {
        Instances.clear();
    }

    void AddEllipse(const EllipseRenderingData& data) {
        Instances.push_back(data);
    }

    std::vector<EllipseRenderingSubmissionData> RecordRendererSubmissionData(size_t ellipseBufferInstanceSizeMax) {
        auto now = std::chrono::high_resolution_clock::now();

        std::ranges::sort(Instances, [](const auto& a, const auto& b) {
            if (a.depth != b.depth) return a.depth < b.depth;
            return a.virtualTextureID < b.virtualTextureID;
        });

        auto sortEnd = std::chrono::high_resolution_clock::now();

        std::vector<EllipseRenderingSubmissionData> submissions;
        if (Instances.empty()) return submissions;

        auto lastFrameSubmissionIt = mLastFrameCache.begin();

        EllipseRenderingSubmissionData currentSubmission;
        if (lastFrameSubmissionIt != mLastFrameCache.end()) {
            currentSubmission = std::move(*lastFrameSubmissionIt);
            currentSubmission.ShapeData.clear();
            ++lastFrameSubmissionIt;
        }

        auto finalizeSubmission = [&]() mutable {
            if (!currentSubmission.ShapeData.empty()) {
                submissions.push_back(std::move(currentSubmission));

                if (lastFrameSubmissionIt == mLastFrameCache.end()) {
                    currentSubmission.Clear();
                } else {
                    currentSubmission = std::move(*lastFrameSubmissionIt);
                    currentSubmission.ShapeData.clear();
                    ++lastFrameSubmissionIt;
                }
            }
        };

        for (const auto& instance : Instances) {
            if (currentSubmission.ShapeData.size() + 1 > ellipseBufferInstanceSizeMax) {
                finalizeSubmission();
            }

            EllipseShapeData shapeData;
            shapeData.center = instance.center;
            shapeData.radii = instance.radii;
            shapeData.rotation = instance.rotation;
            shapeData.innerScale = instance.innerScale;
            shapeData.startAngle = instance.startAngle;
            shapeData.endAngle = instance.endAngle;
            shapeData.tintColor = instance.tintColor;
            shapeData.textureIndex = instance.virtualTextureID;
            shapeData.edgeSoftness = instance.edgeSoftness;
            shapeData._padding = 0.0f;

            currentSubmission.ShapeData.push_back(shapeData);
        }

        finalizeSubmission();

        auto recordEnd = std::chrono::high_resolution_clock::now();
        ImGui::Text("Ellipse Count: %zu", Instances.size());
        ImGui::Text("Ellipse Submissions: %zu", submissions.size());
        ImGui::Text("Ellipse Sorting Time: %.3f ms",
                    std::chrono::duration<float, std::milli>(sortEnd - now).count());
        ImGui::Text("Ellipse Recording Time: %.3f ms",
                    std::chrono::duration<float, std::milli>(recordEnd - sortEnd).count());
        return submissions;
    }

private:
    std::vector<EllipseRenderingSubmissionData> mLastFrameCache;

public:
    void GiveBackForNextFrame(std::vector<EllipseRenderingSubmissionData>&& thisCache) {
        mLastFrameCache = std::move(thisCache);
        mLastFrameCache.resize(0);
    }
};

struct EllipseBatchRenderingResources {
    nvrhi::BufferHandle ShapeBuffer;
    nvrhi::BindingSetHandle mBindingSetSpace0;
};

#pragma endregion

class Renderer2D {
public:
    Renderer2D(Renderer2DDescriptor desc)
        : mDevice(std::move(desc.Device)), mOutputSize(desc.OutputSize), mVirtualSize(desc.VirtualSize),
          mVirtualTextureManager(mDevice) {
        CreateResources();
        CreateConstantBuffers();
        CreatePipelines();
        CreatePipelineResources();
        RecalculateViewProjectionMatrix();
    }

    void CreatePipelineResources();

    void BeginRendering();

    void EndRendering();

    void OnResize(uint32_t width, uint32_t height);

    nvrhi::ITexture *GetTexture() const {
        return mTexture.Get();
    }

    void Clear() {
        mTriangleCommandList.Clear();
        mLineCommandList.Clear();
        mEllipseCommandList.Clear();
    }

    [[nodiscard]] int GetCurrentDepth() const { return mCurrentDepth; }
    void SetCurrentDepth(int depth) { mCurrentDepth = depth; }

    uint32_t RegisterVirtualTextureForThisFrame(const nvrhi::TextureHandle &texture) {
        return mVirtualTextureManager.RegisterTexture(texture);
    }

private:
    nvrhi::DeviceHandle mDevice;

    glm::u32vec2 mOutputSize;
    glm::vec2 mVirtualSize;
    glm::mat4 mViewProjectionMatrix;

    nvrhi::TextureHandle mTexture;
    nvrhi::FramebufferHandle mFramebuffer;

    PersistentVirtualTextureManager mVirtualTextureManager;

    size_t mBindlessTextureArraySizeMax{};

    nvrhi::CommandListHandle mCommandList;
    nvrhi::SamplerHandle mTextureSampler;

    int mCurrentDepth = 0;

    // Fucking triangle, needs so many things
    TriangleRenderingCommandList mTriangleCommandList;
    nvrhi::InputLayoutHandle mTriangleInputLayout;
    nvrhi::GraphicsPipelineHandle mTrianglePipeline;
    nvrhi::BindingLayoutHandle mTriangleBindingLayoutSpace0;
    nvrhi::BindingLayoutHandle mTriangleBindingLayoutSpace1;
    nvrhi::BufferHandle mTriangleConstantBuffer;
    size_t mTriangleBufferInstanceSizeMax;
    std::vector<TriangleBatchRenderingResources> mTriangleBatchRenderingResources;

    // We also want to rendering lines
    LineRenderingCommandList mLineCommandList;
    nvrhi::InputLayoutHandle mLineInputLayout;
    nvrhi::GraphicsPipelineHandle mLinePipeline;
    nvrhi::BindingLayoutHandle mLineBindingLayoutSpace0;
    nvrhi::BufferHandle mLineConstantBuffer;
    size_t mLineBufferVertexSizeMax;
    std::vector<LineBatchRenderingResources> mLineBatchRenderingResources;

    // Ellipse rendering (circles, ellipses, sectors, arcs)
    EllipseRenderingCommandList mEllipseCommandList;
    nvrhi::GraphicsPipelineHandle mEllipsePipeline;
    nvrhi::BindingLayoutHandle mEllipseBindingLayoutSpace0;
    nvrhi::BindingLayoutHandle mEllipseBindingLayoutSpace1;
    nvrhi::BufferHandle mEllipseConstantBuffer;
    size_t mEllipseBufferInstanceSizeMax;
    std::vector<EllipseBatchRenderingResources> mEllipseBatchRenderingResources;

    void CreateResources();

    void CreateTriangleBatchRenderingResources(size_t count);

    void CreateLineBatchRenderingResources(size_t count);

    void CreateEllipseBatchRenderingResources(size_t count);

    void CreatePipelines();

    void CreateConstantBuffers();

    void CreatePipelineTriangle();

    void CreatePipelineLine();

    void CreatePipelineEllipse();

    void SubmitTriangleBatchRendering();

    void SubmitLineBatchRendering();

    void SubmitEllipseBatchRendering();

    void Submit();

    void RecalculateViewProjectionMatrix();

    void SetVirtualSize(const glm::vec2 &virtualSize) {
        mVirtualSize = virtualSize;
        RecalculateViewProjectionMatrix();
    }

public:
    inline void DrawTriangleColored(const glm::mat3x2 &positions,
                                    const glm::u8vec4 &color,
                                    std::optional<int> overrideDepth = std::nullopt) {
        mTriangleCommandList.AddTriangle(
            positions[0], glm::vec2(0.f, 0.f),
            positions[1], glm::vec2(0.f, 0.f),
            positions[2], glm::vec2(0.f, 0.f),
            -1,
            (color.r << 24) | (color.g << 16) | (color.b << 8) | color.a,
            overrideDepth.has_value() ? overrideDepth.value() : mCurrentDepth);
    }

    inline void DrawTriangleTextureVirtual(const glm::mat3x2 &positions,
                                           const glm::mat3x2 &uvs,
                                           uint32_t virtualTextureID,
                                           std::optional<int> overrideDepth = std::nullopt,
                                           glm::u8vec4 tintColor = glm::u8vec4(255, 255, 255, 255)) {
        mTriangleCommandList.AddTriangle(
            positions[0], uvs[0],
            positions[1], uvs[1],
            positions[2], uvs[2],
            static_cast<int>(virtualTextureID),
            (tintColor.r << 24) | (tintColor.g << 16) | (tintColor.b << 8) | tintColor.a,
            overrideDepth.has_value() ? overrideDepth.value() : mCurrentDepth);
    }

    inline uint32_t DrawTriangleTextureManaged(const glm::mat3x2 &positions,
                                               const glm::mat3x2 &uvs,
                                               const nvrhi::TextureHandle &texture,
                                               std::optional<int> overrideDepth = std::nullopt,
                                               glm::u8vec4 tintColor = glm::u8vec4(255, 255, 255, 255)) {
        uint32_t virtualTextureID = RegisterVirtualTextureForThisFrame(texture);
        mTriangleCommandList.AddTriangle(
            positions[0], uvs[0],
            positions[1], uvs[1],
            positions[2], uvs[2],
            static_cast<int>(virtualTextureID),
            (tintColor.r << 24) | (tintColor.g << 16) | (tintColor.b << 8) | tintColor.a,
            overrideDepth.has_value() ? overrideDepth.value() : mCurrentDepth);
        return virtualTextureID;
    }

    inline void DrawQuadColored(const glm::mat4x2 &positions,
                                const glm::u8vec4 &color,
                                std::optional<int> overrideDepth = std::nullopt) {
        mTriangleCommandList.AddQuad(
            positions[0], glm::vec2(0.f, 0.f),
            positions[1], glm::vec2(0.f, 0.f),
            positions[2], glm::vec2(0.f, 0.f),
            positions[3], glm::vec2(0.f, 0.f),
            -1,
            (color.r << 24) | (color.g << 16) | (color.b << 8) | color.a,
            overrideDepth.has_value() ? overrideDepth.value() : mCurrentDepth);
    }

    inline void DrawQuadTextureVirtual(const glm::mat4x2 &positions,
                                       const glm::mat4x2 &uvs,
                                       uint32_t virtualTextureID,
                                       std::optional<int> overrideDepth = std::nullopt,
                                       glm::u8vec4 tintColor = glm::u8vec4(255, 255, 255, 255)) {
        mTriangleCommandList.AddQuad(
            positions[0], uvs[0],
            positions[1], uvs[1],
            positions[2], uvs[2],
            positions[3], uvs[3],
            static_cast<int>(virtualTextureID),
            (tintColor.r << 24) | (tintColor.g << 16) | (tintColor.b << 8) | tintColor.a,
            overrideDepth.has_value() ? overrideDepth.value() : mCurrentDepth);
    }

    inline uint32_t DrawQuadTextureManaged(const glm::mat4x2 &positions,
                                           const glm::mat4x2 &uvs,
                                           const nvrhi::TextureHandle &texture,
                                           std::optional<int> overrideDepth = std::nullopt,
                                           glm::u8vec4 tintColor = glm::u8vec4(255, 255, 255, 255)) {
        uint32_t virtualTextureID = RegisterVirtualTextureForThisFrame(texture);
        mTriangleCommandList.AddQuad(
            positions[0], uvs[0],
            positions[1], uvs[1],
            positions[2], uvs[2],
            positions[3], uvs[3],
            static_cast<int>(virtualTextureID),
            (tintColor.r << 24) | (tintColor.g << 16) | (tintColor.b << 8) | tintColor.a,
            overrideDepth.has_value() ? overrideDepth.value() : mCurrentDepth);
        return virtualTextureID;
    }

    inline void DrawLine(const glm::vec2 &p0, const glm::vec2 &p1,
                         const glm::u8vec4 &color) {
        mLineCommandList.AddLine(p0, color, p1, color);
    }

    inline void DrawLine(const glm::vec2 &p0, const glm::vec2 &p1,
                         const glm::u8vec4 &color0, const glm::u8vec4 &color1) {
        mLineCommandList.AddLine(p0, color0, p1, color1);
    }

    inline void DrawCircle(const glm::vec2& center, float radius,
                          const glm::u8vec4& color,
                          std::optional<int> overrideDepth = std::nullopt) {
        EllipseRenderingData data = EllipseRenderingData::Circle(
            center, radius, color, overrideDepth.value_or(mCurrentDepth));
        mEllipseCommandList.AddEllipse(data);
    }

    inline void DrawEllipse(const glm::vec2& center, const glm::vec2& radii,
                           float rotation, const glm::u8vec4& color,
                           std::optional<int> overrideDepth = std::nullopt) {
        EllipseRenderingData data = EllipseRenderingData::Ellipse(
            center, radii, rotation, color, overrideDepth.value_or(mCurrentDepth));
        mEllipseCommandList.AddEllipse(data);
    }

    inline void DrawRing(const glm::vec2& center, float outerRadius, float innerRadius,
                        const glm::u8vec4& color,
                        std::optional<int> overrideDepth = std::nullopt) {
        EllipseRenderingData data = EllipseRenderingData::Ring(
            center, outerRadius, innerRadius, color, overrideDepth.value_or(mCurrentDepth));
        mEllipseCommandList.AddEllipse(data);
    }

    inline void DrawSector(const glm::vec2& center, float radius,
                          float startAngle, float endAngle,
                          const glm::u8vec4& color,
                          std::optional<int> overrideDepth = std::nullopt) {
        EllipseRenderingData data = EllipseRenderingData::Sector(
            center, radius, startAngle, endAngle, color, -1, overrideDepth.value_or(mCurrentDepth));
        mEllipseCommandList.AddEllipse(data);
    }

    inline void DrawSectorTextured(const glm::vec2& center, float radius,
                                   float startAngle, float endAngle,
                                   uint32_t virtualTextureID,
                                   const glm::u8vec4& tintColor = glm::u8vec4(255, 255, 255, 255),
                                   std::optional<int> overrideDepth = std::nullopt) {
        EllipseRenderingData data = EllipseRenderingData::Sector(
            center, radius, startAngle, endAngle, tintColor,
            static_cast<int>(virtualTextureID), overrideDepth.value_or(mCurrentDepth));
        mEllipseCommandList.AddEllipse(data);
    }

    inline uint32_t DrawSectorTextureManaged(const glm::vec2& center, float radius,
                                             float startAngle, float endAngle,
                                             const nvrhi::TextureHandle& texture,
                                             const glm::u8vec4& tintColor = glm::u8vec4(255, 255, 255, 255),
                                             std::optional<int> overrideDepth = std::nullopt) {
        uint32_t virtualTextureID = RegisterVirtualTextureForThisFrame(texture);
        EllipseRenderingData data = EllipseRenderingData::Sector(
            center, radius, startAngle, endAngle, tintColor,
            static_cast<int>(virtualTextureID), overrideDepth.value_or(mCurrentDepth));
        mEllipseCommandList.AddEllipse(data);
        return virtualTextureID;
    }

    inline void DrawArc(const glm::vec2& center, float radius, float thickness,
                       float startAngle, float endAngle,
                       const glm::u8vec4& color,
                       std::optional<int> overrideDepth = std::nullopt) {
        EllipseRenderingData data = EllipseRenderingData::Arc(
            center, radius, thickness, startAngle, endAngle, color, overrideDepth.value_or(mCurrentDepth));
        mEllipseCommandList.AddEllipse(data);
    }

    inline void DrawEllipseSector(const glm::vec2& center, const glm::vec2& radii,
                                  float rotation, float startAngle, float endAngle,
                                  const glm::u8vec4& color,
                                  std::optional<int> overrideDepth = std::nullopt) {
        EllipseRenderingData data = EllipseRenderingData::EllipseSector(
            center, radii, rotation, startAngle, endAngle, color, -1, overrideDepth.value_or(mCurrentDepth));
        mEllipseCommandList.AddEllipse(data);
    }

    inline void DrawEllipseSectorTextured(const glm::vec2& center, const glm::vec2& radii,
                                          float rotation, float startAngle, float endAngle,
                                          uint32_t virtualTextureID,
                                          const glm::u8vec4& tintColor = glm::u8vec4(255, 255, 255, 255),
                                          std::optional<int> overrideDepth = std::nullopt) {
        EllipseRenderingData data = EllipseRenderingData::EllipseSector(
            center, radii, rotation, startAngle, endAngle, tintColor,
            static_cast<int>(virtualTextureID), overrideDepth.value_or(mCurrentDepth));
        mEllipseCommandList.AddEllipse(data);
    }

    inline void DrawEllipseArc(const glm::vec2& center, const glm::vec2& radii,
                               float rotation, float thickness,
                               float startAngle, float endAngle,
                               const glm::u8vec4& color,
                               std::optional<int> overrideDepth = std::nullopt) {
        EllipseRenderingData data = EllipseRenderingData::EllipseArc(
            center, radii, rotation, thickness, startAngle, endAngle, color, overrideDepth.value_or(mCurrentDepth));
        mEllipseCommandList.AddEllipse(data);
    }

    inline void DrawCircleTextured(const glm::vec2& center, float radius,
                                   uint32_t virtualTextureID,
                                   const glm::u8vec4& tintColor = glm::u8vec4(255, 255, 255, 255),
                                   std::optional<int> overrideDepth = std::nullopt) {
        EllipseRenderingData data;
        data.center = center;
        data.radii = glm::vec2(radius, radius);
        data.virtualTextureID = static_cast<int>(virtualTextureID);
        data.tintColor = (tintColor.r << 24) | (tintColor.g << 16) | (tintColor.b << 8) | tintColor.a;
        data.depth = overrideDepth.value_or(mCurrentDepth);
        mEllipseCommandList.AddEllipse(data);
    }

    inline uint32_t DrawCircleTextureManaged(const glm::vec2& center, float radius,
                                             const nvrhi::TextureHandle& texture,
                                             const glm::u8vec4& tintColor = glm::u8vec4(255, 255, 255, 255),
                                             std::optional<int> overrideDepth = std::nullopt) {
        uint32_t virtualTextureID = RegisterVirtualTextureForThisFrame(texture);
        DrawCircleTextured(center, radius, virtualTextureID, tintColor, overrideDepth);
        return virtualTextureID;
    }

    inline void DrawEllipseTextured(const glm::vec2& center, const glm::vec2& radii,
                                    float rotation, uint32_t virtualTextureID,
                                    const glm::u8vec4& tintColor = glm::u8vec4(255, 255, 255, 255),
                                    std::optional<int> overrideDepth = std::nullopt) {
        EllipseRenderingData data;
        data.center = center;
        data.radii = radii;
        data.rotation = rotation;
        data.virtualTextureID = static_cast<int>(virtualTextureID);
        data.tintColor = (tintColor.r << 24) | (tintColor.g << 16) | (tintColor.b << 8) | tintColor.a;
        data.depth = overrideDepth.value_or(mCurrentDepth);
        mEllipseCommandList.AddEllipse(data);
    }

    inline uint32_t DrawEllipseTextureManaged(const glm::vec2& center, const glm::vec2& radii,
                                              float rotation,
                                              const nvrhi::TextureHandle& texture,
                                              const glm::u8vec4& tintColor = glm::u8vec4(255, 255, 255, 255),
                                              std::optional<int> overrideDepth = std::nullopt) {
        uint32_t virtualTextureID = RegisterVirtualTextureForThisFrame(texture);
        DrawEllipseTextured(center, radii, rotation, virtualTextureID, tintColor, overrideDepth);
        return virtualTextureID;
    }
};

void Renderer2D::CreatePipelineResources() {
    CreateTriangleBatchRenderingResources(4); // this should be enough for most cases, if not we can always expand it
    CreateLineBatchRenderingResources(4); // same for lines
    CreateEllipseBatchRenderingResources(4); // same for ellipses
}

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
    mDevice->executeCommandList(mCommandList);

    if (mVirtualTextureManager.IsSubOptimal()) {
        mVirtualTextureManager.Optimize();
    }
}

void Renderer2D::OnResize(uint32_t width, uint32_t height) {
    if (width == mOutputSize.x && height == mOutputSize.y) {
        return;
    }
    mDevice->waitForIdle();

    mOutputSize = glm::u32vec2(width, height);
    mTexture.Reset();
    mFramebuffer.Reset();

    CreateResources();

    RecalculateViewProjectionMatrix();
}

void Renderer2D::CreateResources() {
    nvrhi::TextureDesc texDesc;
    texDesc.width = mOutputSize.x;
    texDesc.height = mOutputSize.y;
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
    mLineBufferVertexSizeMax = 1 << 18; // 2^18 vertices
    mEllipseBufferInstanceSizeMax = 1 << 16; // 2^16 ellipses (each ellipse = 6 vertices)
}

void Renderer2D::CreateTriangleBatchRenderingResources(size_t count) {
    if (count <= mTriangleBatchRenderingResources.size()) {
        return;
    }

    for (size_t i = mTriangleBatchRenderingResources.size(); i < count; ++i) {
        TriangleBatchRenderingResources resources;

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
        instanceBufferDesc.byteSize = sizeof(TriangleInstanceData) * mTriangleBufferInstanceSizeMax;
        instanceBufferDesc.canHaveRawViews = true;
        instanceBufferDesc.structStride = sizeof(TriangleInstanceData);
        instanceBufferDesc.debugName = "Renderer2D::TriangleInstanceBuffer";
        instanceBufferDesc.initialState = nvrhi::ResourceStates::ShaderResource;
        instanceBufferDesc.keepInitialState = true;
        resources.InstanceBuffer = mDevice->createBuffer(instanceBufferDesc);

        nvrhi::BindingSetDesc bindingSetDesc;
        bindingSetDesc.addItem(nvrhi::BindingSetItem::ConstantBuffer(0, mTriangleConstantBuffer));
        bindingSetDesc.addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(0, resources.InstanceBuffer));
        bindingSetDesc.addItem(nvrhi::BindingSetItem::Sampler(0, mTextureSampler));
        resources.mBindingSetSpace0 = mDevice->createBindingSet(bindingSetDesc, mTriangleBindingLayoutSpace0);

        mTriangleBatchRenderingResources.push_back(resources);
    }
}

void Renderer2D::CreateLineBatchRenderingResources(size_t count) {
    if (count <= mLineBatchRenderingResources.size()) {
        return;
    }

    for (size_t i = mLineBatchRenderingResources.size(); i < count; ++i) {
        LineBatchRenderingResources resources;

        nvrhi::BufferDesc vertexBufferDesc;
        vertexBufferDesc.byteSize = sizeof(LineVertexData) * mLineBufferVertexSizeMax;
        vertexBufferDesc.isVertexBuffer = true;
        vertexBufferDesc.debugName = "Renderer2D::LineVertexBuffer";
        vertexBufferDesc.initialState = nvrhi::ResourceStates::VertexBuffer;
        vertexBufferDesc.keepInitialState = true;
        resources.VertexBuffer = mDevice->createBuffer(vertexBufferDesc);

        nvrhi::BindingSetDesc bindingSetDesc;
        bindingSetDesc.addItem(nvrhi::BindingSetItem::ConstantBuffer(0, mLineConstantBuffer));
        resources.mBindingSetSpace0 = mDevice->createBindingSet(bindingSetDesc, mLineBindingLayoutSpace0);

        mLineBatchRenderingResources.push_back(resources);
    }
}

void Renderer2D::CreateEllipseBatchRenderingResources(size_t count) {
    if (count <= mEllipseBatchRenderingResources.size()) {
        return;
    }

    for (size_t i = mEllipseBatchRenderingResources.size(); i < count; ++i) {
        EllipseBatchRenderingResources resources;

        nvrhi::BufferDesc shapeBufferDesc;
        shapeBufferDesc.byteSize = sizeof(EllipseShapeData) * mEllipseBufferInstanceSizeMax;
        shapeBufferDesc.canHaveRawViews = true;
        shapeBufferDesc.structStride = sizeof(EllipseShapeData);
        shapeBufferDesc.debugName = "Renderer2D::EllipseShapeBuffer";
        shapeBufferDesc.initialState = nvrhi::ResourceStates::ShaderResource;
        shapeBufferDesc.keepInitialState = true;
        resources.ShapeBuffer = mDevice->createBuffer(shapeBufferDesc);

        nvrhi::BindingSetDesc bindingSetDesc;
        bindingSetDesc.addItem(nvrhi::BindingSetItem::ConstantBuffer(0, mEllipseConstantBuffer));
        bindingSetDesc.addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(0, resources.ShapeBuffer));
        bindingSetDesc.addItem(nvrhi::BindingSetItem::Sampler(0, mTextureSampler));
        resources.mBindingSetSpace0 = mDevice->createBindingSet(bindingSetDesc, mEllipseBindingLayoutSpace0);

        mEllipseBatchRenderingResources.push_back(resources);
    }
}

void Renderer2D::CreatePipelines() {
    CreatePipelineTriangle();
    CreatePipelineLine();
    CreatePipelineEllipse();
}

void Renderer2D::CreateConstantBuffers() {
    nvrhi::BufferDesc constBufferVPMatrixDesc;
    constBufferVPMatrixDesc.byteSize = sizeof(glm::mat4);
    constBufferVPMatrixDesc.isConstantBuffer = true;
    constBufferVPMatrixDesc.debugName = "Renderer2D::ConstantBufferVPMatrix";
    constBufferVPMatrixDesc.initialState = nvrhi::ResourceStates::ShaderResource |
                                           nvrhi::ResourceStates::ConstantBuffer;
    constBufferVPMatrixDesc.keepInitialState = true;
    mTriangleConstantBuffer = mDevice->createBuffer(constBufferVPMatrixDesc);

    nvrhi::BufferDesc constBufferLineDesc;
    constBufferLineDesc.byteSize = sizeof(glm::mat4);
    constBufferLineDesc.isConstantBuffer = true;
    constBufferLineDesc.debugName = "Renderer2D::LineConstantBufferVPMatrix";
    constBufferLineDesc.initialState = nvrhi::ResourceStates::ShaderResource |
                                       nvrhi::ResourceStates::ConstantBuffer;
    constBufferLineDesc.keepInitialState = true;
    mLineConstantBuffer = mDevice->createBuffer(constBufferLineDesc);

    nvrhi::BufferDesc constBufferEllipseDesc;
    constBufferEllipseDesc.byteSize = sizeof(glm::mat4);
    constBufferEllipseDesc.isConstantBuffer = true;
    constBufferEllipseDesc.debugName = "Renderer2D::EllipseConstantBufferVPMatrix";
    constBufferEllipseDesc.initialState = nvrhi::ResourceStates::ShaderResource |
                                          nvrhi::ResourceStates::ConstantBuffer;
    constBufferEllipseDesc.keepInitialState = true;
    mEllipseConstantBuffer = mDevice->createBuffer(constBufferEllipseDesc);
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

    mTriangleBindingLayoutSpace0 = mDevice->createBindingLayout(bindingLayoutDesc[0]);
    mTriangleBindingLayoutSpace1 = mDevice->createBindingLayout(bindingLayoutDesc[1]);

    nvrhi::GraphicsPipelineDesc pipeDesc;
    pipeDesc.VS = vs;
    pipeDesc.PS = ps;
    pipeDesc.inputLayout = mTriangleInputLayout;
    pipeDesc.bindingLayouts = {
        mTriangleBindingLayoutSpace0,
        mTriangleBindingLayoutSpace1
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
}

void Renderer2D::CreatePipelineLine() {
    nvrhi::ShaderDesc vsDesc;
    vsDesc.shaderType = nvrhi::ShaderType::Vertex;
    vsDesc.entryName = "main";
    nvrhi::ShaderHandle vs = mDevice->createShader(vsDesc,
                                                   GeneratedShaders::renderer2d_line_vs.data(),
                                                   GeneratedShaders::renderer2d_line_vs.size());

    nvrhi::ShaderDesc psDesc;
    psDesc.shaderType = nvrhi::ShaderType::Pixel;
    psDesc.entryName = "main";
    nvrhi::ShaderHandle ps = mDevice->createShader(psDesc,
                                                   GeneratedShaders::renderer2d_line_ps.data(),
                                                   GeneratedShaders::renderer2d_line_ps.size());

    nvrhi::VertexAttributeDesc posAttrs[2];
    posAttrs[0].name = "POSITION";
    posAttrs[0].format = nvrhi::Format::RG32_FLOAT;
    posAttrs[0].bufferIndex = 0;
    posAttrs[0].offset = offsetof(LineVertexData, position);
    posAttrs[0].elementStride = sizeof(LineVertexData);

    posAttrs[1].name = "COLOR";
    posAttrs[1].format = nvrhi::Format::R32_UINT;
    posAttrs[1].bufferIndex = 0;
    posAttrs[1].offset = offsetof(LineVertexData, color);
    posAttrs[1].elementStride = sizeof(LineVertexData);

    mLineInputLayout = mDevice->createInputLayout(posAttrs, 2, vs);

    nvrhi::BindingLayoutDesc bindingLayoutDesc;
    bindingLayoutDesc.visibility = nvrhi::ShaderType::Vertex;
    bindingLayoutDesc.bindings = {
        nvrhi::BindingLayoutItem::ConstantBuffer(0)
    };

    mLineBindingLayoutSpace0 = mDevice->createBindingLayout(bindingLayoutDesc);

    nvrhi::GraphicsPipelineDesc pipeDesc;
    pipeDesc.VS = vs;
    pipeDesc.PS = ps;
    pipeDesc.inputLayout = mLineInputLayout;
    pipeDesc.bindingLayouts = {
        mLineBindingLayoutSpace0
    };

    pipeDesc.primType = nvrhi::PrimitiveType::LineList;

    pipeDesc.renderState.blendState.targets[0].blendEnable = true;
    pipeDesc.renderState.blendState.targets[0].srcBlend = nvrhi::BlendFactor::SrcAlpha;
    pipeDesc.renderState.blendState.targets[0].destBlend = nvrhi::BlendFactor::InvSrcAlpha;
    pipeDesc.renderState.blendState.targets[0].srcBlendAlpha = nvrhi::BlendFactor::One;
    pipeDesc.renderState.blendState.targets[0].destBlendAlpha = nvrhi::BlendFactor::InvSrcAlpha;
    pipeDesc.renderState.blendState.targets[0].colorWriteMask = nvrhi::ColorMask::All;
    pipeDesc.renderState.rasterState.cullMode = nvrhi::RasterCullMode::None;
    pipeDesc.renderState.depthStencilState.depthTestEnable = false;

    mLinePipeline = mDevice->createGraphicsPipeline(pipeDesc, mFramebuffer->getFramebufferInfo());
}

void Renderer2D::CreatePipelineEllipse() {
    nvrhi::ShaderDesc vsDesc;
    vsDesc.shaderType = nvrhi::ShaderType::Vertex;
    vsDesc.entryName = "main";
    nvrhi::ShaderHandle vs = mDevice->createShader(vsDesc,
                                                   GeneratedShaders::renderer2d_ellipse_vs.data(),
                                                   GeneratedShaders::renderer2d_ellipse_vs.size());

    nvrhi::ShaderDesc psDesc;
    psDesc.shaderType = nvrhi::ShaderType::Pixel;
    psDesc.entryName = "main";
    nvrhi::ShaderHandle ps = mDevice->createShader(psDesc,
                                                   GeneratedShaders::renderer2d_ellipse_ps.data(),
                                                   GeneratedShaders::renderer2d_ellipse_ps.size());

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

    mEllipseBindingLayoutSpace0 = mDevice->createBindingLayout(bindingLayoutDesc[0]);
    mEllipseBindingLayoutSpace1 = mDevice->createBindingLayout(bindingLayoutDesc[1]);

    nvrhi::GraphicsPipelineDesc pipeDesc;
    pipeDesc.VS = vs;
    pipeDesc.PS = ps;
    pipeDesc.bindingLayouts = {
        mEllipseBindingLayoutSpace0,
        mEllipseBindingLayoutSpace1
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

    mEllipsePipeline = mDevice->createGraphicsPipeline(pipeDesc, mFramebuffer->getFramebufferInfo());
}

void Renderer2D::SubmitTriangleBatchRendering() {
    auto submissions = mTriangleCommandList.RecordRendererSubmissionData(
        mTriangleBufferInstanceSizeMax);

    CreateTriangleBatchRenderingResources(submissions.size());

    // submit constant buffer
    mCommandList->writeBuffer(mTriangleConstantBuffer, &mViewProjectionMatrix,
                              sizeof(glm::mat4), 0);

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
                                      sizeof(TriangleInstanceData) * submission.InstanceData.size(), 0);
        }


        mCommandList->setResourceStatesForBindingSet(resources.mBindingSetSpace0);
        auto bindingSetSpace1 = mVirtualTextureManager.GetBindingSet(mTriangleBindingLayoutSpace1);
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

void Renderer2D::SubmitLineBatchRendering() {
    auto submissions = mLineCommandList.RecordRendererSubmissionData(
        mLineBufferVertexSizeMax);

    if (submissions.empty()) {
        return;
    }

    CreateLineBatchRenderingResources(submissions.size());

    // submit constant buffer
    mCommandList->writeBuffer(mLineConstantBuffer, &mViewProjectionMatrix,
                              sizeof(glm::mat4), 0);

    for (size_t i = 0; i < submissions.size(); ++i) {
        auto &submission = submissions[i];
        auto &resources = mLineBatchRenderingResources[i];

        // Update Buffers
        if (!submission.VertexData.empty()) {
            mCommandList->writeBuffer(resources.VertexBuffer, submission.VertexData.data(),
                                      sizeof(LineVertexData) * submission.VertexData.size(), 0);
        } else {
            continue;
        }

        mCommandList->setResourceStatesForBindingSet(resources.mBindingSetSpace0);

        // Draw Call
        nvrhi::GraphicsState state;
        state.pipeline = mLinePipeline;
        state.framebuffer = mFramebuffer;
        state.viewport.addViewportAndScissorRect(
            mFramebuffer->getFramebufferInfo().getViewport());
        state.bindings.push_back(resources.mBindingSetSpace0);

        nvrhi::VertexBufferBinding vertexBufferBinding;
        vertexBufferBinding.buffer = resources.VertexBuffer;
        vertexBufferBinding.offset = 0;
        vertexBufferBinding.slot = 0;

        state.vertexBuffers.push_back(vertexBufferBinding);

        mCommandList->setGraphicsState(state);

        nvrhi::DrawArguments drawArgs;
        drawArgs.vertexCount = static_cast<uint32_t>(submission.VertexData.size());

        mCommandList->draw(drawArgs);
    }

    mLineCommandList.GiveBackForNextFrame(std::move(submissions));
}

void Renderer2D::SubmitEllipseBatchRendering() {
    auto submissions = mEllipseCommandList.RecordRendererSubmissionData(
        mEllipseBufferInstanceSizeMax);

    if (submissions.empty()) {
        return;
    }

    CreateEllipseBatchRenderingResources(submissions.size());

    mCommandList->writeBuffer(mEllipseConstantBuffer, &mViewProjectionMatrix,
                              sizeof(glm::mat4), 0);

    for (size_t i = 0; i < submissions.size(); ++i) {
        auto &submission = submissions[i];
        auto &resources = mEllipseBatchRenderingResources[i];

        if (submission.ShapeData.empty()) {
            continue;
        }

        mCommandList->writeBuffer(resources.ShapeBuffer, submission.ShapeData.data(),
                                  sizeof(EllipseShapeData) * submission.ShapeData.size(), 0);

        mCommandList->setResourceStatesForBindingSet(resources.mBindingSetSpace0);
        auto bindingSetSpace1 = mVirtualTextureManager.GetBindingSet(mEllipseBindingLayoutSpace1);
        mCommandList->setResourceStatesForBindingSet(bindingSetSpace1);

        nvrhi::GraphicsState state;
        state.pipeline = mEllipsePipeline;
        state.framebuffer = mFramebuffer;
        state.viewport.addViewportAndScissorRect(
            mFramebuffer->getFramebufferInfo().getViewport());
        state.bindings.push_back(resources.mBindingSetSpace0);
        state.bindings.push_back(bindingSetSpace1);

        mCommandList->setGraphicsState(state);

        nvrhi::DrawArguments drawArgs;
        drawArgs.vertexCount = static_cast<uint32_t>(submission.ShapeData.size() * 6);

        mCommandList->draw(drawArgs);
    }

    mEllipseCommandList.GiveBackForNextFrame(std::move(submissions));
}

void Renderer2D::Submit() {
    SubmitTriangleBatchRendering();
    SubmitLineBatchRendering();
    SubmitEllipseBatchRendering();
}

void Renderer2D::RecalculateViewProjectionMatrix() {
    float scaleX = static_cast<float>(mOutputSize.x) / mVirtualSize.x;
    float scaleY = static_cast<float>(mOutputSize.y) / mVirtualSize.y;

    float uniformScale = std::min(scaleX, scaleY);

    float halfVisibleWidth = static_cast<float>(mOutputSize.x) / (2.0f * uniformScale);
    float halfVisibleHeight = static_cast<float>(mOutputSize.y) / (2.0f * uniformScale);

    mViewProjectionMatrix = glm::ortho(
        -halfVisibleWidth, // Left
        halfVisibleWidth, // Right
        halfVisibleHeight, // Bottom
        -halfVisibleHeight, // Top
        -1.0f, // zNear
        1.0f // zFar
    );
}

export class RendererDevelopmentLayer : public Layer {
public:
    virtual void OnAttach(const std::shared_ptr<Application> &app) override {
        Layer::OnAttach(app);
        auto &swapchain = mApp->GetSwapchainData();

        Renderer2DDescriptor rendererDesc;
        rendererDesc.Device = mApp->GetNvrhiDevice();
        rendererDesc.OutputSize = {swapchain.GetWidth(), swapchain.GetHeight()};
        rendererDesc.VirtualSize = {1920.0f, 1080.0f};

        mRenderer = std::make_shared<Renderer2D>(std::move(rendererDesc));

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

        const int quadCountHalfX = 150;
        const int quadCountHalfY = 100;

        const float quadSize = 5.0f;
        const float spacing = 0.0f;

        uint32_t texIdRed = mRenderer->RegisterVirtualTextureForThisFrame(mRedTextureHandle);
        uint32_t texIdGreen = mRenderer->RegisterVirtualTextureForThisFrame(mGreenTextureHandle);
        uint32_t texIdBlue = mRenderer->RegisterVirtualTextureForThisFrame(mBlueTextureHandle);

        // for (int y = -quadCountHalfY; y <= quadCountHalfY; ++y) {
        //     for (int x = -quadCountHalfX; x <= quadCountHalfX; ++x) {
        //         float posX = x * (quadSize + spacing);
        //         float posY = y * (quadSize + spacing);
        //
        //         glm::u8vec4 tintColor;
        //         uint32_t texId = texIdRed;
        //
        //         int modResult = ((x + y) % 3 + 3) % 3;
        //
        //         if (modResult == 0) {
        //             texId = texIdRed;
        //             tintColor = glm::u8vec4(255, 255, 255, 127);
        //         } else if (modResult == 1) {
        //             texId = texIdGreen;
        //             tintColor = glm::u8vec4(255, 255, 255, 127);
        //         } else {
        //             texId = texIdBlue;
        //             tintColor = glm::u8vec4(255, 128, 255, 127);
        //         }
        //         mRenderer->DrawQuadTextureVirtual(
        //             glm::mat4x2(
        //                 posX, posY,
        //                 posX + quadSize, posY,
        //                 posX + quadSize, posY + quadSize,
        //                 posX, posY + quadSize
        //             ),
        //             glm::mat4x2(
        //                 0.f, 0.f,
        //                 1.f, 0.f,
        //                 1.f, 1.f,
        //                 0.f, 1.f
        //             ),
        //             texId,
        //             std::nullopt,
        //             tintColor
        //         );
        //     }
        // }

        // Test line rendering (commented out for circle tests)
        // mRenderer->DrawLine(
        //     glm::vec2(-800.0f, -400.0f),
        //     glm::vec2(-400.0f, 0.0f),
        //     glm::u8vec4(255, 0, 0, 255)
        // );
        //
        // mRenderer->DrawLine(
        //     glm::vec2(-400.0f, -400.0f),
        //     glm::vec2(0.0f, 0.0f),
        //     glm::u8vec4(0, 255, 0, 255)
        // );
        //
        // mRenderer->DrawLine(
        //     glm::vec2(0.0f, -400.0f),
        //     glm::vec2(400.0f, 0.0f),
        //     glm::u8vec4(0, 0, 255, 255)
        // );
        //
        // mRenderer->DrawLine(
        //     glm::vec2(400.0f, -400.0f),
        //     glm::vec2(800.0f, 0.0f),
        //     glm::u8vec4(255, 255, 0, 255)
        // );
        //
        // mRenderer->DrawLine(
        //     glm::vec2(-800.0f, 0.0f),
        //     glm::vec2(-400.0f, 400.0f),
        //     glm::u8vec4(0, 255, 255, 255)
        // );
        //
        // mRenderer->DrawLine(
        //     glm::vec2(400.0f, 0.0f),
        //     glm::vec2(800.0f, 400.0f),
        //     glm::u8vec4(255, 0, 255, 255)
        // );

        // Test circle/ellipse rendering
        // Basic solid circles
        mRenderer->DrawCircle(glm::vec2(-400.0f, -200.0f), 80.0f, glm::u8vec4(255, 0, 0, 255));
        mRenderer->DrawCircle(glm::vec2(-200.0f, -200.0f), 60.0f, glm::u8vec4(0, 255, 0, 255));
        mRenderer->DrawCircle(glm::vec2(0.0f, -200.0f), 70.0f, glm::u8vec4(0, 0, 255, 255));
        mRenderer->DrawCircle(glm::vec2(200.0f, -200.0f), 50.0f, glm::u8vec4(255, 255, 0, 255));

        // Circles with transparency
        mRenderer->DrawCircle(glm::vec2(-300.0f, 0.0f), 90.0f, glm::u8vec4(255, 0, 255, 128));
        mRenderer->DrawCircle(glm::vec2(-100.0f, 0.0f), 85.0f, glm::u8vec4(0, 255, 255, 128));

        // Ellipses with rotation
        mRenderer->DrawEllipse(glm::vec2(150.0f, 0.0f), glm::vec2(100.0f, 50.0f), 0.0f,
                              glm::u8vec4(255, 128, 0, 255));
        mRenderer->DrawEllipse(glm::vec2(350.0f, 0.0f), glm::vec2(100.0f, 50.0f), 0.785f,
                              glm::u8vec4(128, 0, 255, 255));

        // Rings
        mRenderer->DrawRing(glm::vec2(-400.0f, 250.0f), 80.0f, 50.0f, glm::u8vec4(255, 0, 0, 255));
        mRenderer->DrawRing(glm::vec2(-200.0f, 250.0f), 70.0f, 50.0f, glm::u8vec4(0, 255, 0, 255));

        // Sectors (pie slices)
        const float PI = 3.14159265359f;
        mRenderer->DrawSector(glm::vec2(0.0f, 250.0f), 80.0f, 0.0f, PI * 0.5f,
                             glm::u8vec4(255, 255, 0, 255));
        mRenderer->DrawSector(glm::vec2(200.0f, 250.0f), 80.0f, PI * 0.25f, PI * 1.25f,
                             glm::u8vec4(0, 255, 255, 255));

        // Arcs (ring segments)
        mRenderer->DrawArc(glm::vec2(400.0f, 250.0f), 80.0f, 15.0f, 0.0f, PI * 1.5f,
                          glm::u8vec4(255, 0, 255, 255));

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
