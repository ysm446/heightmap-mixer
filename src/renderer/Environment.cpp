#include "renderer/Environment.h"

#include "core/ImageIo.h"
#include "core/Log.h"

#include <pix3.h>

#include <algorithm>
#include <cstring>

using namespace DirectX;

namespace mm::renderer {
namespace {

constexpr uint32_t kCubeSize = 256;
constexpr uint32_t kIrradianceSize = 32;
constexpr uint32_t kPrefilteredSize = 128;
constexpr uint32_t kPrefilteredMipCount = 6;
constexpr uint32_t kBrdfLutSize = 256;

constexpr uint32_t kDefaultEquirectWidth = 1024;
constexpr uint32_t kDefaultEquirectHeight = 512;

// equirect は読み込んだ HDR をそのまま入れられるよう 32bit float にする。
constexpr DXGI_FORMAT kEquirectFormat = DXGI_FORMAT_R32G32B32A32_FLOAT;
constexpr DXGI_FORMAT kRadianceFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
constexpr DXGI_FORMAT kBrdfLutFormat = DXGI_FORMAT_R16G16_FLOAT;

constexpr uint32_t kGroupSize = 8;

// これらは PS（メッシュ・スカイボックス）と CS（プリフィルタ）の両方から読むため、
// 読み取り状態をまとめて指定する。
constexpr D3D12_RESOURCE_STATES kShaderReadState =
    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

constexpr uint32_t kIrradianceSampleCount = 512;
constexpr uint32_t kPrefilterSampleCount = 256;
constexpr uint32_t kBrdfLutSampleCount = 1024;

uint32_t MipCountFor(uint32_t size) {
    uint32_t count = 1;
    while (size > 1) {
        size >>= 1;
        ++count;
    }
    return count;
}

uint32_t DispatchCount(uint32_t threads) {
    return (threads + kGroupSize - 1) / kGroupSize;
}

// ミップ mip の全スライスをまとめて遷移させる。
void TransitionMip(ID3D12GraphicsCommandList* commandList, const rhi::GpuTexture& texture,
                   uint32_t mip, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER barriers[6] = {};
    const uint32_t sliceCount = std::min<uint32_t>(texture.arraySize, 6);
    for (uint32_t slice = 0; slice < sliceCount; ++slice) {
        barriers[slice] = CD3DX12_RESOURCE_BARRIER::Transition(
            texture.resource.Get(), before, after, texture.SubresourceIndex(mip, slice));
    }
    commandList->ResourceBarrier(sliceCount, barriers);
}

void TransitionAll(ID3D12GraphicsCommandList* commandList, rhi::GpuTexture& texture,
                   D3D12_RESOURCE_STATES after) {
    if (texture.state == after) {
        return;
    }
    const auto barrier =
        CD3DX12_RESOURCE_BARRIER::Transition(texture.resource.Get(), texture.state, after);
    commandList->ResourceBarrier(1, &barrier);
    texture.state = after;
}

void InsertUavBarrier(ID3D12GraphicsCommandList* commandList, const rhi::GpuTexture& texture) {
    const auto barrier = CD3DX12_RESOURCE_BARRIER::UAV(texture.resource.Get());
    commandList->ResourceBarrier(1, &barrier);
}

void ReleaseTexture(rhi::Device& device, rhi::GpuTexture& texture) {
    if (!texture.IsValid()) {
        return;
    }
    device.Allocator().ReleaseDescriptors(texture);
    device.Defer(texture.resource);
    device.Defer(texture.allocation);
    texture = rhi::GpuTexture{};
}

}  // namespace

bool Environment::Initialize(rhi::Device& device, rhi::PipelineCache& pipelineCache) {
    rhi::TextureDesc brdfDesc;
    brdfDesc.width = kBrdfLutSize;
    brdfDesc.height = kBrdfLutSize;
    brdfDesc.format = kBrdfLutFormat;
    brdfDesc.allowUnorderedAccess = true;
    brdfDesc.createSrv = true;
    brdfDesc.initialState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    brdfDesc.debugName = L"EnvBrdfLut";
    if (!device.Allocator().CreateTexture2D(brdfDesc, m_brdfLut)) {
        return false;
    }
    if (!BuildBrdfLut(device, pipelineCache)) {
        return false;
    }

    return BuildFromSky(device, pipelineCache, SkySettings{});
}

void Environment::Shutdown(rhi::Device& device) {
    ReleaseTargets(device);
    ReleaseTexture(device, m_brdfLut);
    m_ready = false;
}

void Environment::ReleaseTargets(rhi::Device& device) {
    ReleaseTexture(device, m_equirect);
    ReleaseTexture(device, m_cube);
    ReleaseTexture(device, m_irradiance);
    ReleaseTexture(device, m_prefiltered);
}

bool Environment::BuildBrdfLut(rhi::Device& device, rhi::PipelineCache& pipelineCache) {
    ID3D12PipelineState* pipeline = pipelineCache.GetCompute(L"EnvBrdfLut.hlsl", L"CsMain");
    if (pipeline == nullptr) {
        return false;
    }

    struct BrdfLutConstants {
        uint32_t outputIndex;
        uint32_t size;
        uint32_t sampleCount;
    };
    const BrdfLutConstants constants{m_brdfLut.UavIndex(), kBrdfLutSize, kBrdfLutSampleCount};

    const bool executed = device.ExecuteImmediate([&](ID3D12GraphicsCommandList* commandList) {
        PIXBeginEvent(commandList, PIX_COLOR(120, 180, 255), "EnvBrdfLut");
        commandList->SetComputeRootSignature(pipelineCache.GlobalRootSignature());
        commandList->SetPipelineState(pipeline);
        commandList->SetComputeRoot32BitConstants(0, sizeof(constants) / sizeof(uint32_t),
                                                  &constants, 0);
        commandList->Dispatch(DispatchCount(kBrdfLutSize), DispatchCount(kBrdfLutSize), 1);
        TransitionAll(commandList, m_brdfLut, kShaderReadState);
        PIXEndEvent(commandList);
    });
    return executed;
}

bool Environment::CreateTargets(rhi::Device& device, uint32_t equirectWidth,
                                uint32_t equirectHeight) {
    ReleaseTargets(device);

    rhi::TextureDesc equirectDesc;
    equirectDesc.width = equirectWidth;
    equirectDesc.height = equirectHeight;
    equirectDesc.format = kEquirectFormat;
    equirectDesc.allowUnorderedAccess = true;
    equirectDesc.createSrv = true;
    equirectDesc.initialState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    equirectDesc.debugName = L"EnvEquirect";
    if (!device.Allocator().CreateTexture2D(equirectDesc, m_equirect)) {
        return false;
    }

    rhi::TextureDesc cubeDesc;
    cubeDesc.width = kCubeSize;
    cubeDesc.height = kCubeSize;
    cubeDesc.mipLevels = MipCountFor(kCubeSize);
    cubeDesc.isCube = true;
    cubeDesc.format = kRadianceFormat;
    cubeDesc.allowUnorderedAccess = true;
    cubeDesc.createMipUavs = true;
    cubeDesc.createMipSrvs = true;
    cubeDesc.createSrv = true;
    cubeDesc.initialState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    cubeDesc.debugName = L"EnvCube";
    if (!device.Allocator().CreateTexture2D(cubeDesc, m_cube)) {
        return false;
    }

    rhi::TextureDesc irradianceDesc;
    irradianceDesc.width = kIrradianceSize;
    irradianceDesc.height = kIrradianceSize;
    irradianceDesc.isCube = true;
    irradianceDesc.format = kRadianceFormat;
    irradianceDesc.allowUnorderedAccess = true;
    irradianceDesc.createSrv = true;
    irradianceDesc.initialState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    irradianceDesc.debugName = L"EnvIrradiance";
    if (!device.Allocator().CreateTexture2D(irradianceDesc, m_irradiance)) {
        return false;
    }

    rhi::TextureDesc prefilteredDesc;
    prefilteredDesc.width = kPrefilteredSize;
    prefilteredDesc.height = kPrefilteredSize;
    prefilteredDesc.mipLevels = kPrefilteredMipCount;
    prefilteredDesc.isCube = true;
    prefilteredDesc.format = kRadianceFormat;
    prefilteredDesc.allowUnorderedAccess = true;
    prefilteredDesc.createMipUavs = true;
    prefilteredDesc.createSrv = true;
    prefilteredDesc.initialState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    prefilteredDesc.debugName = L"EnvPrefiltered";
    if (!device.Allocator().CreateTexture2D(prefilteredDesc, m_prefiltered)) {
        return false;
    }

    return true;
}

bool Environment::BuildFromEquirect(rhi::Device& device, rhi::PipelineCache& pipelineCache) {
    ID3D12PipelineState* toCubePipeline =
        pipelineCache.GetCompute(L"EnvEquirectToCube.hlsl", L"CsMain");
    ID3D12PipelineState* downsamplePipeline =
        pipelineCache.GetCompute(L"EnvDownsample.hlsl", L"CsMain");
    ID3D12PipelineState* irradiancePipeline =
        pipelineCache.GetCompute(L"EnvIrradiance.hlsl", L"CsMain");
    ID3D12PipelineState* prefilterPipeline =
        pipelineCache.GetCompute(L"EnvPrefilter.hlsl", L"CsMain");
    if (toCubePipeline == nullptr || downsamplePipeline == nullptr ||
        irradiancePipeline == nullptr || prefilterPipeline == nullptr) {
        return false;
    }

    const uint32_t cubeMipCount = m_cube.mipLevels;
    // irradiance は粗いミップから引く。ノイズが減り、精度も十分。
    const uint32_t irradianceSourceMip = std::min<uint32_t>(cubeMipCount - 1, 4);

    const bool executed = device.ExecuteImmediate([&](ID3D12GraphicsCommandList* commandList) {
        ID3D12RootSignature* rootSignature = pipelineCache.GlobalRootSignature();
        commandList->SetComputeRootSignature(rootSignature);

        // --- equirect → キューブ ミップ 0 ---------------------------------
        PIXBeginEvent(commandList, PIX_COLOR(120, 180, 255), "EnvEquirectToCube");
        TransitionAll(commandList, m_equirect, kShaderReadState);

        struct EquirectToCubeConstants {
            uint32_t sourceIndex;
            uint32_t outputIndex;
            uint32_t faceSize;
        };
        const EquirectToCubeConstants toCubeConstants{m_equirect.SrvIndex(),
                                                      m_cube.MipUavIndex(0), kCubeSize};

        commandList->SetPipelineState(toCubePipeline);
        commandList->SetComputeRoot32BitConstants(
            0, sizeof(toCubeConstants) / sizeof(uint32_t), &toCubeConstants, 0);
        commandList->Dispatch(DispatchCount(kCubeSize), DispatchCount(kCubeSize), 6);
        InsertUavBarrier(commandList, m_cube);
        PIXEndEvent(commandList);

        // --- キューブのミップ連鎖 -----------------------------------------
        // 読むミップだけをサブリソース単位で読み取り状態にする。
        PIXBeginEvent(commandList, PIX_COLOR(120, 180, 255), "EnvCubeMips");
        commandList->SetPipelineState(downsamplePipeline);

        struct DownsampleConstants {
            uint32_t sourceIndex;
            uint32_t outputIndex;
            uint32_t outputSize;
        };

        uint32_t mipSize = kCubeSize;
        for (uint32_t mip = 1; mip < cubeMipCount; ++mip) {
            TransitionMip(commandList, m_cube, mip - 1, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                          kShaderReadState);

            mipSize = std::max<uint32_t>(mipSize >> 1, 1);
            const DownsampleConstants constants{m_cube.MipSrvIndex(mip - 1),
                                                m_cube.MipUavIndex(mip), mipSize};
            commandList->SetComputeRoot32BitConstants(0, sizeof(constants) / sizeof(uint32_t),
                                                      &constants, 0);
            commandList->Dispatch(DispatchCount(mipSize), DispatchCount(mipSize), 6);
            InsertUavBarrier(commandList, m_cube);
        }

        // 最後のミップも読み取り状態へ移し、リソース全体を揃える。
        TransitionMip(commandList, m_cube, cubeMipCount - 1,
                      D3D12_RESOURCE_STATE_UNORDERED_ACCESS, kShaderReadState);
        m_cube.state = kShaderReadState;
        PIXEndEvent(commandList);

        // --- irradiance ---------------------------------------------------
        PIXBeginEvent(commandList, PIX_COLOR(120, 180, 255), "EnvIrradiance");
        struct IrradianceConstants {
            uint32_t sourceIndex;
            uint32_t outputIndex;
            uint32_t faceSize;
            uint32_t sampleCount;
            uint32_t sourceMip;
        };
        const IrradianceConstants irradianceConstants{m_cube.SrvIndex(),
                                                      m_irradiance.MipUavIndex(0),
                                                      kIrradianceSize, kIrradianceSampleCount,
                                                      irradianceSourceMip};

        commandList->SetPipelineState(irradiancePipeline);
        commandList->SetComputeRoot32BitConstants(
            0, sizeof(irradianceConstants) / sizeof(uint32_t), &irradianceConstants, 0);
        commandList->Dispatch(DispatchCount(kIrradianceSize), DispatchCount(kIrradianceSize), 6);
        TransitionAll(commandList, m_irradiance, kShaderReadState);
        PIXEndEvent(commandList);

        // --- プリフィルタ済み鏡面 ------------------------------------------
        PIXBeginEvent(commandList, PIX_COLOR(120, 180, 255), "EnvPrefilter");
        commandList->SetPipelineState(prefilterPipeline);

        struct PrefilterConstants {
            uint32_t sourceIndex;
            uint32_t outputIndex;
            uint32_t faceSize;
            uint32_t sourceSize;
            uint32_t sampleCount;
            float roughness;
        };

        uint32_t prefilterSize = kPrefilteredSize;
        for (uint32_t mip = 0; mip < kPrefilteredMipCount; ++mip) {
            const float roughness =
                static_cast<float>(mip) / static_cast<float>(kPrefilteredMipCount - 1);
            const PrefilterConstants constants{m_cube.SrvIndex(),
                                               m_prefiltered.MipUavIndex(mip),
                                               prefilterSize,
                                               kCubeSize,
                                               kPrefilterSampleCount,
                                               roughness};
            commandList->SetComputeRoot32BitConstants(0, sizeof(constants) / sizeof(uint32_t),
                                                      &constants, 0);
            commandList->Dispatch(DispatchCount(prefilterSize), DispatchCount(prefilterSize), 6);
            InsertUavBarrier(commandList, m_prefiltered);
            prefilterSize = std::max<uint32_t>(prefilterSize >> 1, 1);
        }
        TransitionAll(commandList, m_prefiltered, kShaderReadState);
        PIXEndEvent(commandList);
    });

    if (!executed) {
        return false;
    }

    m_ready = true;
    return true;
}

bool Environment::BuildFromSky(rhi::Device& device, rhi::PipelineCache& pipelineCache,
                               const SkySettings& sky) {
    if (!CreateTargets(device, kDefaultEquirectWidth, kDefaultEquirectHeight)) {
        return false;
    }

    ID3D12PipelineState* skyPipeline = pipelineCache.GetCompute(L"EnvSky.hlsl", L"CsMain");
    if (skyPipeline == nullptr) {
        return false;
    }

    struct SkyConstants {
        uint32_t outputIndex;
        uint32_t width;
        uint32_t height;
        float intensity;
        XMFLOAT3 zenithColor;
        float pad0;
        XMFLOAT3 horizonColor;
        float pad1;
        XMFLOAT3 groundColor;
        float pad2;
    };
    SkyConstants constants = {};
    constants.outputIndex = m_equirect.UavIndex();
    constants.width = m_equirect.width;
    constants.height = m_equirect.height;
    constants.intensity = sky.intensity;
    constants.zenithColor = sky.zenithColor;
    constants.horizonColor = sky.horizonColor;
    constants.groundColor = sky.groundColor;

    const bool executed = device.ExecuteImmediate([&](ID3D12GraphicsCommandList* commandList) {
        PIXBeginEvent(commandList, PIX_COLOR(120, 180, 255), "EnvSky");
        commandList->SetComputeRootSignature(pipelineCache.GlobalRootSignature());
        commandList->SetPipelineState(skyPipeline);
        commandList->SetComputeRoot32BitConstants(0, sizeof(constants) / sizeof(uint32_t),
                                                  &constants, 0);
        commandList->Dispatch(DispatchCount(m_equirect.width), DispatchCount(m_equirect.height), 1);
        PIXEndEvent(commandList);
    });
    if (!executed) {
        return false;
    }

    m_sourceName = "手続き的な空";
    return BuildFromEquirect(device, pipelineCache);
}

bool Environment::BuildFromHdrFile(rhi::Device& device, rhi::PipelineCache& pipelineCache,
                                   const std::filesystem::path& path) {
    HdrImage image;
    if (!LoadHdrImage(path, image)) {
        return false;
    }

    if (!CreateTargets(device, image.width, image.height)) {
        return false;
    }

    // アップロード用の中間バッファを用意し、行ピッチを合わせて詰め直す。
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
    UINT rowCount = 0;
    UINT64 rowSizeInBytes = 0;
    UINT64 totalBytes = 0;
    const D3D12_RESOURCE_DESC resourceDesc = m_equirect.resource->GetDesc();
    device.GetDevice()->GetCopyableFootprints(&resourceDesc, 0, 1, 0, &footprint, &rowCount,
                                              &rowSizeInBytes, &totalBytes);

    rhi::GpuBuffer staging;
    if (!device.Allocator().CreateUploadBuffer(totalBytes, L"EnvEquirectStaging", staging)) {
        return false;
    }

    void* mapped = nullptr;
    const D3D12_RANGE readRange = {0, 0};
    if (!MM_CHECK_HR(staging.resource->Map(0, &readRange, &mapped))) {
        return false;
    }
    auto* destination = static_cast<uint8_t*>(mapped) + footprint.Offset;
    const auto* source = reinterpret_cast<const uint8_t*>(image.pixels.data());
    for (uint32_t row = 0; row < rowCount; ++row) {
        std::memcpy(destination + static_cast<size_t>(row) * footprint.Footprint.RowPitch,
                    source + static_cast<size_t>(row) * image.RowPitchInBytes(),
                    static_cast<size_t>(rowSizeInBytes));
    }
    staging.resource->Unmap(0, nullptr);

    const bool executed = device.ExecuteImmediate([&](ID3D12GraphicsCommandList* commandList) {
        PIXBeginEvent(commandList, PIX_COLOR(120, 180, 255), "EnvUploadHdr");
        TransitionAll(commandList, m_equirect, D3D12_RESOURCE_STATE_COPY_DEST);

        const CD3DX12_TEXTURE_COPY_LOCATION destinationLocation(m_equirect.resource.Get(), 0);
        const CD3DX12_TEXTURE_COPY_LOCATION sourceLocation(staging.resource.Get(), footprint);
        commandList->CopyTextureRegion(&destinationLocation, 0, 0, 0, &sourceLocation, nullptr);
        PIXEndEvent(commandList);
    });
    if (!executed) {
        return false;
    }

    // ステージングバッファは GPU の完了後に解放する。
    device.Defer(staging.resource);
    device.Defer(staging.allocation);

    m_sourceName = path.filename().string();
    return BuildFromEquirect(device, pipelineCache);
}

}  // namespace mm::renderer
