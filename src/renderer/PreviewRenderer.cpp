#include "renderer/PreviewRenderer.h"

#include "core/ImageIo.h"
#include "core/Log.h"

#include <pix3.h>

#include <cmath>
#include <cstring>

using namespace DirectX;

namespace hm::renderer {
namespace {

constexpr DXGI_FORMAT kSceneColorFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
constexpr DXGI_FORMAT kDepthFormat = DXGI_FORMAT_D32_FLOAT;
constexpr DXGI_FORMAT kOutputFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

// GPU 側の MeshConstants と一致させること。
struct MeshConstants {
    XMFLOAT4X4 viewProjection;
    XMFLOAT4X4 model;
    XMFLOAT4X4 normalMatrix;

    XMFLOAT3 cameraPosition;
    float pad0;

    XMFLOAT3 lightDirection;
    float lightIlluminance;

    XMFLOAT3 lightColor;
    float pad1;

    XMFLOAT3 baseColor;
    float roughness;

    float metallic;
    float iblIntensity;
    uint32_t prefilteredMipCount;
    float pad2;

    uint32_t irradianceIndex;
    uint32_t prefilteredIndex;
    uint32_t brdfLutIndex;
    uint32_t useMaterialTextures;

    uint32_t materialBaseColorIndex;
    uint32_t materialNormalIndex;
    uint32_t materialSurfaceIndex;
    uint32_t materialHeightIndex;

    float materialUvScale;
    float pad4[3];
};

// GPU 側の SkyboxConstants と一致させること。
struct SkyboxConstants {
    XMFLOAT4X4 inverseViewProjection;

    XMFLOAT3 cameraPosition;
    float intensity;

    uint32_t environmentIndex;
    uint32_t mipLevel;
    float pad0[2];
};

struct TonemapConstants {
    uint32_t sourceIndex;
    uint32_t outputIndex;
    uint32_t width;
    uint32_t height;
    float exposure;
    uint32_t tonemapMode;
};

void TransitionIfNeeded(ID3D12GraphicsCommandList* commandList, rhi::GpuTexture& texture,
                        D3D12_RESOURCE_STATES newState) {
    if (texture.state == newState) {
        return;
    }
    const auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(texture.resource.Get(),
                                                              texture.state, newState);
    commandList->ResourceBarrier(1, &barrier);
    texture.state = newState;
}

}  // namespace

float ExposureSettings::Ev100() const {
    if (useManualEv) {
        return manualEv100;
    }
    const float safeAperture = (aperture > 0.0f) ? aperture : 1.0f;
    const float safeShutter = (shutterSpeed > 0.0f) ? shutterSpeed : 1.0f;
    const float safeIso = (iso > 0.0f) ? iso : 100.0f;
    return std::log2((safeAperture * safeAperture) / safeShutter) - std::log2(safeIso / 100.0f);
}

float ExposureSettings::Exposure() const {
    return 1.0f / (1.2f * std::pow(2.0f, Ev100()));
}

XMFLOAT3 LightSettings::Direction() const {
    const float cosElevation = std::cos(elevation);
    return XMFLOAT3{cosElevation * std::sin(azimuth), std::sin(elevation),
                    cosElevation * std::cos(azimuth)};
}

bool PreviewRenderer::Initialize(rhi::Device& device, rhi::PipelineCache& pipelineCache) {
    if (!m_sphere.Create(device, MakeSphere(64, 32, 1.0f), L"SphereMesh")) {
        return false;
    }
    if (!m_plane.Create(device, MakePlane(2.0f, 32), L"PlaneMesh")) {
        return false;
    }
    if (!m_cube.Create(device, MakeCube(1.4f), L"CubeMesh")) {
        return false;
    }
    if (!m_environment.Initialize(device, pipelineCache)) {
        return false;
    }
    if (!m_evaluator.Create(device, m_materialResolution)) {
        return false;
    }
    return true;
}

void PreviewRenderer::Shutdown(rhi::Device& device) {
    m_evaluator.Destroy(device);
    m_environment.Shutdown(device);
    m_sphere.Release(device);
    m_plane.Release(device);
    m_cube.Release(device);
    ReleaseTargets(device);
}

void PreviewRenderer::ProcessPendingWork(rhi::Device& device,
                                        rhi::PipelineCache& pipelineCache) {
    if (m_requestedMaterialResolution != m_materialResolution) {
        if (m_evaluator.Resize(device, m_requestedMaterialResolution)) {
            m_materialResolution = m_requestedMaterialResolution;
        } else {
            m_requestedMaterialResolution = m_materialResolution;
        }
    }

    if (!m_pendingHdrPath.empty()) {
        const std::filesystem::path path = m_pendingHdrPath;
        m_pendingHdrPath.clear();
        if (!m_environment.BuildFromHdrFile(device, pipelineCache, path)) {
            // 読み込みに失敗したら手続き的な空へ戻す。
            HM_LOG_WARN("HDRI の読み込みに失敗したため、手続き的な空に戻します");
            m_environment.BuildFromSky(device, pipelineCache, m_sky);
        }
        m_skyRebuildRequested = false;
        return;
    }

    if (m_skyRebuildRequested) {
        m_skyRebuildRequested = false;
        m_environment.BuildFromSky(device, pipelineCache, m_sky);
    }
}

const Mesh& PreviewRenderer::CurrentMesh() const {
    switch (m_shape) {
        case PreviewShape::Plane: return m_plane;
        case PreviewShape::Cube:  return m_cube;
        case PreviewShape::Sphere:
        default:                  return m_sphere;
    }
}

void PreviewRenderer::ReleaseTargets(rhi::Device& device) {
    rhi::GpuTexture* targets[] = {&m_sceneColor, &m_depth, &m_output};
    for (rhi::GpuTexture* target : targets) {
        if (!target->IsValid()) {
            continue;
        }
        device.Allocator().ReleaseDescriptors(*target);
        device.Defer(target->resource);
        device.Defer(target->allocation);
        *target = rhi::GpuTexture{};
    }
    m_width = 0;
    m_height = 0;
}

bool PreviewRenderer::SaveOutputToPng(rhi::Device& device, const std::filesystem::path& path) {
    if (!m_output.IsValid()) {
        return false;
    }

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
    UINT rowCount = 0;
    UINT64 rowSizeInBytes = 0;
    UINT64 totalBytes = 0;
    const D3D12_RESOURCE_DESC resourceDesc = m_output.resource->GetDesc();
    device.GetDevice()->GetCopyableFootprints(&resourceDesc, 0, 1, 0, &footprint, &rowCount,
                                              &rowSizeInBytes, &totalBytes);

    rhi::GpuBuffer readback;
    if (!device.Allocator().CreateReadbackBuffer(totalBytes, L"PreviewReadback", readback)) {
        return false;
    }

    const D3D12_RESOURCE_STATES previousState = m_output.state;
    const bool executed = device.ExecuteImmediate([&](ID3D12GraphicsCommandList* commandList) {
        TransitionIfNeeded(commandList, m_output, D3D12_RESOURCE_STATE_COPY_SOURCE);

        const CD3DX12_TEXTURE_COPY_LOCATION destination(readback.resource.Get(), footprint);
        const CD3DX12_TEXTURE_COPY_LOCATION source(m_output.resource.Get(), 0);
        commandList->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);

        TransitionIfNeeded(commandList, m_output, previousState);
    });
    if (!executed) {
        return false;
    }

    void* mapped = nullptr;
    const D3D12_RANGE readRange = {0, static_cast<SIZE_T>(totalBytes)};
    if (!HM_CHECK_HR(readback.resource->Map(0, &readRange, &mapped))) {
        return false;
    }

    const bool saved =
        SaveRgba8Png(path, m_width, m_height, footprint.Footprint.RowPitch,
                     static_cast<const uint8_t*>(mapped) + footprint.Offset);

    const D3D12_RANGE writtenRange = {0, 0};
    readback.resource->Unmap(0, &writtenRange);

    device.Defer(readback.resource);
    device.Defer(readback.allocation);
    return saved;
}

bool PreviewRenderer::Resize(rhi::Device& device, uint32_t width, uint32_t height) {
    if (width == 0 || height == 0) {
        return false;
    }
    if (width == m_width && height == m_height) {
        return true;
    }

    // 作り直す前に、GPU がまだ参照しているターゲットを解放できる状態にする。
    device.WaitForGpu();
    ReleaseTargets(device);

    rhi::TextureDesc colorDesc;
    colorDesc.width = width;
    colorDesc.height = height;
    colorDesc.format = kSceneColorFormat;
    colorDesc.allowRenderTarget = true;
    colorDesc.createSrv = true;
    colorDesc.initialState = D3D12_RESOURCE_STATE_RENDER_TARGET;
    colorDesc.debugName = L"SceneColor";
    if (!device.Allocator().CreateTexture2D(colorDesc, m_sceneColor)) {
        return false;
    }

    rhi::TextureDesc depthDesc;
    depthDesc.width = width;
    depthDesc.height = height;
    depthDesc.format = kDepthFormat;
    depthDesc.allowDepthStencil = true;
    depthDesc.createSrv = false;
    depthDesc.clearDepth = 1.0f;
    depthDesc.initialState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    depthDesc.debugName = L"SceneDepth";
    if (!device.Allocator().CreateTexture2D(depthDesc, m_depth)) {
        return false;
    }

    rhi::TextureDesc outputDesc;
    outputDesc.width = width;
    outputDesc.height = height;
    outputDesc.format = kOutputFormat;
    outputDesc.allowUnorderedAccess = true;
    outputDesc.createSrv = true;
    outputDesc.initialState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    outputDesc.debugName = L"PreviewOutput";
    if (!device.Allocator().CreateTexture2D(outputDesc, m_output)) {
        return false;
    }

    m_width = width;
    m_height = height;
    m_camera.SetViewportSize(width, height);
    return true;
}

void PreviewRenderer::Render(rhi::Device& device, rhi::PipelineCache& pipelineCache,
                             ID3D12GraphicsCommandList* commandList,
                             const compositor::MaterialStack& stack) {
    if (!m_sceneColor.IsValid() || !m_output.IsValid()) {
        return;
    }

    // レイヤースタックに変更があれば、メッシュを描く前に評価し直す。
    m_evaluator.EvaluateIfDirty(device, pipelineCache, commandList, stack);

    rhi::GraphicsPipelineDesc meshPipelineDesc;
    meshPipelineDesc.shaderPath = L"MeshPbr.hlsl";
    meshPipelineDesc.vertexEntry = L"VsMain";
    meshPipelineDesc.pixelEntry = L"PsMain";
    meshPipelineDesc.rtvFormat = kSceneColorFormat;
    meshPipelineDesc.dsvFormat = kDepthFormat;
    meshPipelineDesc.layout = rhi::VertexLayout::MeshStandard;
    meshPipelineDesc.cullMode = D3D12_CULL_MODE_BACK;

    ID3D12PipelineState* meshPipeline = pipelineCache.GetGraphics(meshPipelineDesc);
    ID3D12PipelineState* tonemapPipeline =
        pipelineCache.GetCompute(L"TonemapPass.hlsl", L"CsMain");
    if (meshPipeline == nullptr || tonemapPipeline == nullptr) {
        return;
    }

    const Mesh& mesh = CurrentMesh();
    if (!mesh.IsValid()) {
        return;
    }

    PIXBeginEvent(commandList, PIX_COLOR(80, 200, 120), "PreviewScene");

    TransitionIfNeeded(commandList, m_sceneColor, D3D12_RESOURCE_STATE_RENDER_TARGET);
    TransitionIfNeeded(commandList, m_depth, D3D12_RESOURCE_STATE_DEPTH_WRITE);

    const D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_sceneColor.rtv.cpu;
    const D3D12_CPU_DESCRIPTOR_HANDLE dsv = m_depth.dsv.cpu;
    commandList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);

    const float clearColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    commandList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
    commandList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    const auto viewport = CD3DX12_VIEWPORT(0.0f, 0.0f, static_cast<float>(m_width),
                                           static_cast<float>(m_height));
    const auto scissor = CD3DX12_RECT(0, 0, static_cast<LONG>(m_width),
                                      static_cast<LONG>(m_height));
    commandList->RSSetViewports(1, &viewport);
    commandList->RSSetScissorRects(1, &scissor);

    // DirectXMath は行ベクトル規約、HLSL の行列は既定で列優先。
    // XMMATRIX をそのまま積むと HLSL 側では転置として解釈され、
    // mul(matrix, vector) が意図どおりの結果になる。転置は入れない。
    const XMMATRIX view = m_camera.ViewMatrix();
    const XMMATRIX projection = m_camera.ProjectionMatrix();
    const XMMATRIX model = XMMatrixIdentity();

    MeshConstants constants = {};
    XMStoreFloat4x4(&constants.viewProjection, XMMatrixMultiply(view, projection));
    XMStoreFloat4x4(&constants.model, model);
    // 法線行列だけは (M^-1)^T が要るため、転置を明示する。
    XMStoreFloat4x4(&constants.normalMatrix,
                    XMMatrixTranspose(XMMatrixInverse(nullptr, model)));

    constants.cameraPosition = m_camera.Position();
    constants.lightDirection = m_light.Direction();
    constants.lightIlluminance = m_light.illuminance;
    constants.lightColor = m_light.color;
    constants.baseColor = m_material.baseColor;
    constants.roughness = m_material.roughness;
    constants.metallic = m_material.metallic;
    constants.iblIntensity = m_environment.IsReady() ? m_iblIntensity : 0.0f;
    constants.prefilteredMipCount = m_environment.PrefilteredMipCount();
    constants.irradianceIndex = m_environment.IrradianceSrvIndex();
    constants.prefilteredIndex = m_environment.PrefilteredSrvIndex();
    constants.brdfLutIndex = m_environment.BrdfLutSrvIndex();

    const compositor::MaterialTextureSet& materialTextures = m_evaluator.Textures();
    const bool useMaterial = m_useMaterialTextures && materialTextures.IsValid();
    constants.useMaterialTextures = useMaterial ? 1u : 0u;
    constants.materialBaseColorIndex = materialTextures.baseColor.SrvIndex();
    constants.materialNormalIndex = materialTextures.normal.SrvIndex();
    constants.materialSurfaceIndex = materialTextures.surface.SrvIndex();
    constants.materialHeightIndex = materialTextures.height.SrvIndex();
    constants.materialUvScale = m_materialUvScale;

    const rhi::UploadAllocation cb = device.Upload().Allocate(sizeof(MeshConstants), 256);
    if (!cb.IsValid()) {
        PIXEndEvent(commandList);
        return;
    }
    std::memcpy(cb.cpu, &constants, sizeof(constants));

    commandList->SetGraphicsRootSignature(pipelineCache.GlobalRootSignature());
    commandList->SetPipelineState(meshPipeline);
    commandList->SetGraphicsRootConstantBufferView(1, cb.gpuAddress);
    mesh.Draw(commandList);

    PIXEndEvent(commandList);

    // --- スカイボックス ----------------------------------------------------
    // メッシュのあとに描く。深度は書かず、まだ何も描かれていない画素だけを埋める。
    if (m_showSkybox && m_environment.IsReady()) {
        rhi::GraphicsPipelineDesc skyboxPipelineDesc;
        skyboxPipelineDesc.shaderPath = L"Skybox.hlsl";
        skyboxPipelineDesc.vertexEntry = L"VsMain";
        skyboxPipelineDesc.pixelEntry = L"PsMain";
        skyboxPipelineDesc.rtvFormat = kSceneColorFormat;
        skyboxPipelineDesc.dsvFormat = kDepthFormat;
        skyboxPipelineDesc.layout = rhi::VertexLayout::None;
        skyboxPipelineDesc.cullMode = D3D12_CULL_MODE_NONE;
        skyboxPipelineDesc.depthTest = true;
        skyboxPipelineDesc.depthWrite = false;

        ID3D12PipelineState* skyboxPipeline = pipelineCache.GetGraphics(skyboxPipelineDesc);
        const rhi::UploadAllocation skyboxCb =
            device.Upload().Allocate(sizeof(SkyboxConstants), 256);

        if (skyboxPipeline != nullptr && skyboxCb.IsValid()) {
            PIXBeginEvent(commandList, PIX_COLOR(120, 160, 220), "PreviewSkybox");

            SkyboxConstants skyboxConstants = {};
            // メッシュ側と同じ理由で転置は入れない。
            XMStoreFloat4x4(&skyboxConstants.inverseViewProjection,
                            XMMatrixInverse(nullptr, XMMatrixMultiply(view, projection)));
            skyboxConstants.cameraPosition = m_camera.Position();
            skyboxConstants.intensity = m_iblIntensity;
            skyboxConstants.environmentIndex = m_environment.EnvironmentSrvIndex();
            skyboxConstants.mipLevel = 0;
            std::memcpy(skyboxCb.cpu, &skyboxConstants, sizeof(skyboxConstants));

            commandList->SetPipelineState(skyboxPipeline);
            commandList->SetGraphicsRootConstantBufferView(1, skyboxCb.gpuAddress);
            commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            commandList->IASetVertexBuffers(0, 0, nullptr);
            commandList->IASetIndexBuffer(nullptr);
            commandList->DrawInstanced(3, 1, 0, 0);

            PIXEndEvent(commandList);
        }
    }

    PIXBeginEvent(commandList, PIX_COLOR(200, 120, 80), "PreviewTonemap");

    TransitionIfNeeded(commandList, m_sceneColor, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    TransitionIfNeeded(commandList, m_output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    const TonemapConstants tonemapConstants{
        m_sceneColor.SrvIndex(), m_output.UavIndex(),        m_width,
        m_height,                m_exposure.Exposure(),      static_cast<uint32_t>(m_tonemap)};

    commandList->SetComputeRootSignature(pipelineCache.GlobalRootSignature());
    commandList->SetPipelineState(tonemapPipeline);
    commandList->SetComputeRoot32BitConstants(0, sizeof(tonemapConstants) / sizeof(uint32_t),
                                              &tonemapConstants, 0);

    constexpr uint32_t kGroupSize = 8;
    commandList->Dispatch((m_width + kGroupSize - 1) / kGroupSize,
                          (m_height + kGroupSize - 1) / kGroupSize, 1);

    // ImGui から SRV として読むため、ピクセルシェーダ可視の状態へ移す。
    TransitionIfNeeded(commandList, m_output, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    PIXEndEvent(commandList);
}

}  // namespace hm::renderer
