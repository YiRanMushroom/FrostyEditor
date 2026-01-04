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

import Render.GeneratedShaders;

using namespace Engine;

export class RendererDevelopmentLayer : public Layer {
    virtual void OnAttach(const std::shared_ptr<Application> &app) override {
        Layer::OnAttach(app);

        auto &device = mApp->GetNvrhiDevice();
        nvrhi::ShaderDesc vsDesc;
        vsDesc.shaderType = nvrhi::ShaderType::Vertex;
        vsDesc.entryName = "main";
        vsDesc.debugName = "TriangleVS";
        nvrhi::ShaderHandle vertexShader = device->createShader(vsDesc, GeneratedShaders::simple_vb_triangle_vs.data(),
                                                                GeneratedShaders::simple_vb_triangle_vs.size());

        nvrhi::ShaderDesc psDesc;
        psDesc.shaderType = nvrhi::ShaderType::Pixel;
        psDesc.entryName = "main";
        psDesc.debugName = "TrianglePS";
        nvrhi::ShaderHandle pixelShader = device->createShader(psDesc, GeneratedShaders::simple_vb_triangle_ps.data(),
                                                               GeneratedShaders::simple_vb_triangle_ps.size());

        // Create vertex buffer
        struct Vertex {
            float position[2];
        };

        Vertex vertices[] = {
            {{0.0f, 0.5f}},
            {{-0.5f, -0.5f}},
            {{0.5f, -0.5f}}
        };

        nvrhi::BufferDesc vbDesc;
        vbDesc.byteSize = sizeof(vertices);
        vbDesc.debugName = "TriangleVertexBuffer";
        vbDesc.isVertexBuffer = true;
        vbDesc.canHaveUAVs = false;
        vbDesc.initialState = nvrhi::ResourceStates::VertexBuffer;
        vbDesc.keepInitialState = true;

        mVertexBuffer = device->createBuffer(vbDesc);

        auto &commandList = mApp->GetCommandList();
        commandList->open();
        commandList->writeBuffer(mVertexBuffer, vertices, sizeof(vertices));
        commandList->close();
        mApp->GetNvrhiDevice()->executeCommandList(commandList);

        // Create pipeline
        nvrhi::VertexAttributeDesc vertexAttribute = {
            "POSITION",
            nvrhi::Format::RG32_FLOAT,
            1,
            0,
            0,
            sizeof(Vertex),
            false
        };

        nvrhi::InputLayoutHandle inputLayout = device->createInputLayout(&vertexAttribute, 1, vertexShader);
        nvrhi::GraphicsPipelineDesc pipelineDesc;
        pipelineDesc.primType = nvrhi::PrimitiveType::TriangleList;
        pipelineDesc.inputLayout = inputLayout;

        pipelineDesc.VS = vertexShader;
        pipelineDesc.PS = pixelShader;

        pipelineDesc.renderState.rasterState.cullMode = nvrhi::RasterCullMode::None;
        pipelineDesc.renderState.rasterState.fillMode = nvrhi::RasterFillMode::Solid;

        pipelineDesc.renderState.depthStencilState.depthTestEnable = false;
        pipelineDesc.renderState.depthStencilState.stencilEnable = false;

        pipelineDesc.renderState.blendState.targets[0].blendEnable = true;

        pipelineDesc.renderState.blendState.targets[0].srcBlend = nvrhi::BlendFactor::SrcAlpha;
        pipelineDesc.renderState.blendState.targets[0].destBlend = nvrhi::BlendFactor::InvSrcAlpha;
        pipelineDesc.renderState.blendState.targets[0].blendOp = nvrhi::BlendOp::Add;

        pipelineDesc.renderState.blendState.targets[0].srcBlendAlpha = nvrhi::BlendFactor::One;
        pipelineDesc.renderState.blendState.targets[0].destBlendAlpha = nvrhi::BlendFactor::InvSrcAlpha;
        pipelineDesc.renderState.blendState.targets[0].blendOpAlpha = nvrhi::BlendOp::Add;

        pipelineDesc.renderState.blendState.targets[0].colorWriteMask = nvrhi::ColorMask::All;

        nvrhi::FramebufferInfo fbInfo;
        fbInfo.colorFormats.push_back(app->GetSwapchainData().GetNvrhiFormat());
        fbInfo.sampleCount = 1;

        mPipeline = device->createGraphicsPipeline(pipelineDesc, fbInfo);
    }

    virtual void OnRender(const nvrhi::CommandListHandle &commandList,
                          const nvrhi::FramebufferHandle &framebuffer) override {
        Layer::OnRender(commandList, framebuffer);

        nvrhi::GraphicsState graphicsState;
        graphicsState.framebuffer = framebuffer;
        graphicsState.pipeline = mPipeline;

        nvrhi::VertexBufferBinding vbBinding;
        vbBinding.buffer = mVertexBuffer;
        vbBinding.offset = 0;
        vbBinding.slot = 0;
        graphicsState.vertexBuffers = {vbBinding};

        nvrhi::ViewportState viewportState;
        viewportState.scissorRects = {
            nvrhi::Rect(
                framebuffer->getDesc().colorAttachments[0].texture->getDesc().width,
                framebuffer->getDesc().colorAttachments[0].texture->getDesc().height)
        };
        viewportState.viewports = {
            nvrhi::Viewport(
                static_cast<float>(framebuffer->getDesc().colorAttachments[0].texture->getDesc().width),
                static_cast<float>(framebuffer->getDesc().colorAttachments[0].texture->getDesc().height))
        };
        graphicsState.viewport = viewportState;

        commandList->setGraphicsState(graphicsState);
        nvrhi::DrawArguments drawArgs;
        drawArgs.vertexCount = 3;
        commandList->draw(drawArgs);
    }

private:
    nvrhi::GraphicsPipelineHandle mPipeline;
    nvrhi::BufferHandle mVertexBuffer;
};
