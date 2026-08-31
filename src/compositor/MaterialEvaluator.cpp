#include "compositor/MaterialEvaluator.h"

#include "core/Log.h"

#include <pix3.h>

#include <algorithm>
#include <cstring>

using namespace DirectX;

namespace hm::compositor {
namespace {

constexpr uint32_t kGroupSize = 8;

constexpr DXGI_FORMAT kBaseColorFormat = DXGI_FORMAT_R11G11B10_FLOAT;
constexpr DXGI_FORMAT kNormalFormat = DXGI_FORMAT_R16G16_FLOAT;
constexpr DXGI_FORMAT kSurfaceFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
constexpr DXGI_FORMAT kHeightFormat = DXGI_FORMAT_R16_FLOAT;

constexpr uint32_t kFlagMaskInvert = 0x1u;
constexpr uint32_t kFlagBaseLayer = 0x2u;

// GPU 側の LayerConstants と一致させること。
struct LayerConstants {
    uint32_t outputIndices[4];
    uint32_t tile[4];
    uint32_t resolution[2];
    uint32_t channelMask;
    uint32_t flags;

    float baseColor[4];
    float surfaceParams[4];
    float blendParams[4];
    float maskParams[4];
    float heightNoise[4];
    float maskNoise[4];
    uint32_t textureIndices0[4];
    uint32_t textureIndices1[4];
};

uint32_t DispatchCount(uint32_t threads) {
    return (threads + kGroupSize - 1) / kGroupSize;
}

bool CreateChannelTexture(rhi::Device& device, uint32_t resolution, DXGI_FORMAT format,
                          const wchar_t* debugName, rhi::GpuTexture& outTexture) {
    rhi::TextureDesc desc;
    desc.width = resolution;
    desc.height = resolution;
    desc.format = format;
    desc.allowUnorderedAccess = true;
    desc.createSrv = true;
    desc.initialState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    desc.debugName = debugName;
    return device.Allocator().CreateTexture2D(desc, outTexture);
}

void TransitionIfNeeded(ID3D12GraphicsCommandList* commandList, rhi::GpuTexture& texture,
                        D3D12_RESOURCE_STATES newState) {
    if (texture.state == newState) {
        return;
    }
    const auto barrier =
        CD3DX12_RESOURCE_BARRIER::Transition(texture.resource.Get(), texture.state, newState);
    commandList->ResourceBarrier(1, &barrier);
    texture.state = newState;
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

bool MaterialEvaluator::Create(rhi::Device& device, uint32_t resolution) {
    ReleaseTextures(device);

    if (!CreateChannelTexture(device, resolution, kBaseColorFormat, L"MaterialBaseColor",
                              m_textures.baseColor) ||
        !CreateChannelTexture(device, resolution, kNormalFormat, L"MaterialNormal",
                              m_textures.normal) ||
        !CreateChannelTexture(device, resolution, kSurfaceFormat, L"MaterialSurface",
                              m_textures.surface) ||
        !CreateChannelTexture(device, resolution, kHeightFormat, L"MaterialHeight",
                              m_textures.height)) {
        return false;
    }

    m_resolution = resolution;
    m_evaluatedRevision = 0;
    return true;
}

void MaterialEvaluator::Destroy(rhi::Device& device) {
    ReleaseTextures(device);
    m_resolution = 0;
    m_evaluatedRevision = 0;
}

void MaterialEvaluator::ReleaseTextures(rhi::Device& device) {
    ReleaseTexture(device, m_textures.baseColor);
    ReleaseTexture(device, m_textures.normal);
    ReleaseTexture(device, m_textures.surface);
    ReleaseTexture(device, m_textures.height);
}

bool MaterialEvaluator::Resize(rhi::Device& device, uint32_t resolution) {
    if (resolution == m_resolution) {
        return true;
    }
    // 作り直す前に GPU の参照が切れるのを待つ。
    device.WaitForGpu();
    return Create(device, resolution);
}

void MaterialEvaluator::Evaluate(rhi::Device& device, rhi::PipelineCache& pipelineCache,
                                 ID3D12GraphicsCommandList* commandList,
                                 const MaterialStack& stack, const TextureLibrary& textures,
                                 const TileRect& tile) {
    if (!m_textures.IsValid() || tile.width == 0 || tile.height == 0) {
        return;
    }

    ID3D12PipelineState* pipeline = pipelineCache.GetCompute(L"CompositeLayer.hlsl", L"CsMain");
    if (pipeline == nullptr) {
        return;
    }

    const size_t baseIndex = stack.FirstEnabledIndex();
    if (baseIndex == static_cast<size_t>(-1)) {
        return;
    }

    PIXBeginEvent(commandList, PIX_COLOR(220, 140, 60), "CompositeStack");

    TransitionIfNeeded(commandList, m_textures.baseColor, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    TransitionIfNeeded(commandList, m_textures.normal, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    TransitionIfNeeded(commandList, m_textures.surface, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    TransitionIfNeeded(commandList, m_textures.height, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    commandList->SetComputeRootSignature(pipelineCache.GlobalRootSignature());
    commandList->SetPipelineState(pipeline);

    m_evaluatedLayerCount = 0;

    for (size_t i = 0; i < stack.Layers().size(); ++i) {
        const MaterialLayer& layer = stack.Layers()[i];
        if (!layer.enabled) {
            continue;
        }

        const bool isBaseLayer = (i == baseIndex);

        LayerConstants constants = {};
        constants.outputIndices[0] = m_textures.baseColor.UavIndex();
        constants.outputIndices[1] = m_textures.normal.UavIndex();
        constants.outputIndices[2] = m_textures.surface.UavIndex();
        constants.outputIndices[3] = m_textures.height.UavIndex();

        constants.tile[0] = tile.x;
        constants.tile[1] = tile.y;
        constants.tile[2] = tile.width;
        constants.tile[3] = tile.height;

        constants.resolution[0] = m_resolution;
        constants.resolution[1] = m_resolution;

        // 一番下のレイヤーは下地なので、必ず全チャンネルを埋める。
        // そうしないと未初期化のテクセルが残る。
        constants.channelMask = isBaseLayer ? kAllChannelBits : layer.channelMask;

        constants.flags = 0;
        if (layer.mask.invert) {
            constants.flags |= kFlagMaskInvert;
        }
        if (isBaseLayer) {
            constants.flags |= kFlagBaseLayer;
        }

        constants.baseColor[0] = layer.baseColor.x;
        constants.baseColor[1] = layer.baseColor.y;
        constants.baseColor[2] = layer.baseColor.z;

        constants.surfaceParams[0] = layer.roughness;
        constants.surfaceParams[1] = layer.metallic;
        constants.surfaceParams[2] = layer.ambientOcclusion;
        constants.surfaceParams[3] = layer.heightBase;

        constants.blendParams[0] = layer.blendRange;
        constants.blendParams[1] = layer.normalStrength;
        constants.blendParams[2] = layer.uvScale;
        constants.blendParams[3] = static_cast<float>(layer.heightSource);

        constants.maskParams[0] = layer.mask.constant;
        constants.maskParams[1] = layer.mask.levelsLow;
        constants.maskParams[2] = layer.mask.levelsHigh;
        constants.maskParams[3] = static_cast<float>(layer.mask.source);

        constants.heightNoise[0] = layer.heightNoise.scale;
        constants.heightNoise[1] = layer.heightNoise.amount;
        constants.heightNoise[2] = static_cast<float>(layer.heightNoise.octaves);
        constants.heightNoise[3] = layer.heightNoise.offset;

        constants.maskNoise[0] = layer.mask.noise.scale;
        constants.maskNoise[1] = layer.mask.noise.amount;
        constants.maskNoise[2] = static_cast<float>(layer.mask.noise.octaves);
        constants.maskNoise[3] = layer.mask.noise.offset;

        // ベースカラーだけ sRGB として読む。それ以外はリニア。
        constants.textureIndices0[0] = textures.SrvIndex(layer.textures.baseColor, true);
        constants.textureIndices0[1] = textures.SrvIndex(layer.textures.normal, false);
        constants.textureIndices0[2] = textures.SrvIndex(layer.textures.roughness, false);
        constants.textureIndices0[3] = textures.SrvIndex(layer.textures.metallic, false);
        constants.textureIndices1[0] = textures.SrvIndex(layer.textures.ambientOcclusion, false);
        constants.textureIndices1[1] = textures.SrvIndex(layer.textures.height, false);
        constants.textureIndices1[2] = textures.SrvIndex(layer.textures.mask, false);
        constants.textureIndices1[3] = kInvalidTextureIndex;

        const rhi::UploadAllocation cb = device.Upload().Allocate(sizeof(LayerConstants), 256);
        if (!cb.IsValid()) {
            break;
        }
        std::memcpy(cb.cpu, &constants, sizeof(constants));

        commandList->SetComputeRootConstantBufferView(1, cb.gpuAddress);
        commandList->Dispatch(DispatchCount(tile.width), DispatchCount(tile.height), 1);

        // 次のレイヤーは前のレイヤーの結果を読むので、必ず区切る。
        const D3D12_RESOURCE_BARRIER barriers[] = {
            CD3DX12_RESOURCE_BARRIER::UAV(m_textures.baseColor.resource.Get()),
            CD3DX12_RESOURCE_BARRIER::UAV(m_textures.normal.resource.Get()),
            CD3DX12_RESOURCE_BARRIER::UAV(m_textures.surface.resource.Get()),
            CD3DX12_RESOURCE_BARRIER::UAV(m_textures.height.resource.Get()),
        };
        commandList->ResourceBarrier(_countof(barriers), barriers);

        ++m_evaluatedLayerCount;
    }

    // メッシュのピクセルシェーダから読めるようにする。
    TransitionIfNeeded(commandList, m_textures.baseColor,
                       D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    TransitionIfNeeded(commandList, m_textures.normal, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    TransitionIfNeeded(commandList, m_textures.surface,
                       D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    TransitionIfNeeded(commandList, m_textures.height, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    PIXEndEvent(commandList);
}

void MaterialEvaluator::EvaluateIfDirty(rhi::Device& device, rhi::PipelineCache& pipelineCache,
                                        ID3D12GraphicsCommandList* commandList,
                                        const MaterialStack& stack,
                                        const TextureLibrary& textures) {
    if (m_evaluatedRevision == stack.Revision()) {
        return;
    }

    m_evaluatedTileCount = 0;
    for (uint32_t y = 0; y < m_resolution; y += m_tileSize) {
        for (uint32_t x = 0; x < m_resolution; x += m_tileSize) {
            TileRect tile;
            tile.x = x;
            tile.y = y;
            tile.width = std::min(m_tileSize, m_resolution - x);
            tile.height = std::min(m_tileSize, m_resolution - y);
            Evaluate(device, pipelineCache, commandList, stack, textures, tile);
            ++m_evaluatedTileCount;
        }
    }

    m_evaluatedRevision = stack.Revision();
}

}  // namespace hm::compositor
