#include "compositor/MaterialLibrary.h"

#include "core/Log.h"

#include <pix3.h>

#include <algorithm>

namespace hm::compositor {
namespace {

constexpr uint32_t kGroupSize = 8;
// サムネイルの一辺。一覧で並べる大きさに対して十分で、VRAM も食わない。
constexpr uint32_t kThumbnailSize = 128;
// サムネイルの中でマップを何回並べるか。1 枚だと模様の粒が分かりにくい。
constexpr float kThumbnailUvScale = 2.0f;

// GPU 側の ThumbnailConstants と一致させること。
struct ThumbnailConstants {
    uint32_t outputIndex;
    uint32_t size;
    uint32_t baseColorIndex;
    uint32_t normalIndex;

    uint32_t roughnessIndex;
    uint32_t metallicIndex;
    uint32_t aoIndex;
    uint32_t heightIndex;

    float baseColorTint[3];
    float roughnessValue;

    float metallicValue;
    float aoValue;
    float uvScale;
    uint32_t mapChannels;
};

}  // namespace

uint32_t PackMaterialChannels(const MaterialAsset& asset) {
    // 並びは HM_CHANNEL_SLOT_* と一致させること。
    return PackChannel(asset.roughness.channel, 0) | PackChannel(asset.metallic.channel, 1) |
           PackChannel(asset.ambientOcclusion.channel, 2) | PackChannel(asset.height.channel, 3);
}

namespace {

uint32_t DispatchCount(uint32_t threads) {
    return (threads + kGroupSize - 1) / kGroupSize;
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

void MaterialLibrary::Destroy(rhi::Device& device) {
    for (MaterialAsset& asset : m_entries) {
        ReleaseTexture(device, asset.thumbnail);
    }
    m_entries.clear();
}

void MaterialLibrary::Clear(rhi::Device& device) {
    device.WaitForGpu();
    Destroy(device);
}

MaterialAssetId MaterialLibrary::Add(const std::string& name) {
    MaterialAsset asset;
    asset.id = m_nextId++;
    asset.name = name;
    m_entries.push_back(std::move(asset));
    return m_entries.back().id;
}

MaterialAssetId MaterialLibrary::Duplicate(const MaterialAsset& source) {
    MaterialAsset asset = source;
    asset.id = m_nextId++;
    asset.name = source.name + " のコピー";
    // サムネイルは共有しない。作り直させる。
    asset.thumbnail = rhi::GpuTexture{};
    asset.thumbnailDirty = true;
    m_entries.push_back(std::move(asset));
    return m_entries.back().id;
}

void MaterialLibrary::Remove(rhi::Device& device, MaterialAssetId id) {
    const auto it = std::find_if(m_entries.begin(), m_entries.end(),
                                 [id](const MaterialAsset& asset) { return asset.id == id; });
    if (it == m_entries.end()) {
        return;
    }
    // ディスクリプタを解放するので、GPU がサムネイルを読み終わるまで待つ。
    device.WaitForGpu();
    ReleaseTexture(device, it->thumbnail);
    m_entries.erase(it);
}

const MaterialAsset* MaterialLibrary::Find(MaterialAssetId id) const {
    if (id == kNoMaterialAsset) {
        return nullptr;
    }
    for (const MaterialAsset& asset : m_entries) {
        if (asset.id == id) {
            return &asset;
        }
    }
    return nullptr;
}

MaterialAsset* MaterialLibrary::FindMutable(MaterialAssetId id) {
    return const_cast<MaterialAsset*>(Find(id));
}

void MaterialLibrary::AssignOrdTexture(MaterialAssetId id, TextureId texture) {
    MaterialAsset* asset = FindMutable(id);
    if (asset == nullptr) {
        return;
    }
    // Megascans の `_ORD` は O = Occlusion(R) / R = Roughness(G) / D = Displacement(B)。
    asset->ambientOcclusion = MapSlot{texture, TextureChannel::R};
    asset->roughness = MapSlot{texture, TextureChannel::G};
    asset->height = MapSlot{texture, TextureChannel::B};
    asset->thumbnailDirty = true;
}

void MaterialLibrary::MarkThumbnailDirty(MaterialAssetId id) {
    if (MaterialAsset* asset = FindMutable(id); asset != nullptr) {
        asset->thumbnailDirty = true;
    }
}

D3D12_GPU_DESCRIPTOR_HANDLE MaterialLibrary::ThumbnailHandle(MaterialAssetId id) const {
    const MaterialAsset* asset = Find(id);
    if (asset == nullptr || !asset->thumbnail.IsValid()) {
        return D3D12_GPU_DESCRIPTOR_HANDLE{0};
    }
    return asset->thumbnail.srv.gpu;
}

void MaterialLibrary::ProcessPendingWork(rhi::Device& device, rhi::PipelineCache& pipelineCache,
                                         const TextureLibrary& textures) {
    const bool anyDirty = std::any_of(m_entries.begin(), m_entries.end(),
                                      [](const MaterialAsset& a) { return a.thumbnailDirty; });
    if (!anyDirty) {
        return;
    }

    for (MaterialAsset& asset : m_entries) {
        if (!asset.thumbnailDirty) {
            continue;
        }
        if (BuildThumbnail(device, pipelineCache, textures, asset)) {
            asset.thumbnailDirty = false;
        } else {
            // 失敗を繰り返さないよう、要求は落とす。
            asset.thumbnailDirty = false;
            HM_LOG_WARN("マテリアル「%s」のサムネイルを作れませんでした", asset.name.c_str());
        }
    }
}

bool MaterialLibrary::BuildThumbnail(rhi::Device& device, rhi::PipelineCache& pipelineCache,
                                     const TextureLibrary& textures, MaterialAsset& asset) {
    ID3D12PipelineState* pipeline =
        pipelineCache.GetCompute(L"MaterialThumbnail.hlsl", L"CsMain");
    if (pipeline == nullptr) {
        return false;
    }

    if (!asset.thumbnail.IsValid()) {
        rhi::TextureDesc desc;
        desc.width = kThumbnailSize;
        desc.height = kThumbnailSize;
        desc.format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.allowUnorderedAccess = true;
        desc.createSrv = true;
        desc.initialState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        desc.debugName = L"MaterialThumbnail";
        if (!device.Allocator().CreateTexture2D(desc, asset.thumbnail)) {
            return false;
        }
    }

    ThumbnailConstants constants = {};
    constants.outputIndex = asset.thumbnail.UavIndex();
    constants.size = kThumbnailSize;
    // ベースカラーだけ sRGB として読む。それ以外はリニア。
    constants.baseColorIndex = textures.SrvIndex(asset.baseColor, true);
    constants.normalIndex = textures.SrvIndex(asset.normal, false);
    constants.roughnessIndex = textures.SrvIndex(asset.roughness.texture, false);
    constants.metallicIndex = textures.SrvIndex(asset.metallic.texture, false);
    constants.aoIndex = textures.SrvIndex(asset.ambientOcclusion.texture, false);
    constants.heightIndex = textures.SrvIndex(asset.height.texture, false);
    constants.mapChannels = PackMaterialChannels(asset);
    constants.baseColorTint[0] = asset.baseColorTint.x;
    constants.baseColorTint[1] = asset.baseColorTint.y;
    constants.baseColorTint[2] = asset.baseColorTint.z;
    constants.roughnessValue = asset.roughnessValue;
    constants.metallicValue = asset.metallicValue;
    constants.aoValue = asset.ambientOcclusionValue;
    constants.uvScale = kThumbnailUvScale;

    rhi::GpuTexture& thumbnail = asset.thumbnail;
    const bool executed = device.ExecuteImmediate([&](ID3D12GraphicsCommandList* commandList) {
        PIXBeginEvent(commandList, PIX_COLOR(120, 200, 200), "MaterialThumbnail");

        if (thumbnail.state != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
            const auto toUav = CD3DX12_RESOURCE_BARRIER::Transition(
                thumbnail.resource.Get(), thumbnail.state,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            commandList->ResourceBarrier(1, &toUav);
        }

        commandList->SetComputeRootSignature(pipelineCache.GlobalRootSignature());
        commandList->SetPipelineState(pipeline);
        commandList->SetComputeRoot32BitConstants(0, sizeof(constants) / sizeof(uint32_t),
                                                  &constants, 0);
        commandList->Dispatch(DispatchCount(kThumbnailSize), DispatchCount(kThumbnailSize), 1);

        // ImGui から SRV として読むので、ピクセルシェーダ可視の状態へ移す。
        const auto toRead = CD3DX12_RESOURCE_BARRIER::Transition(
            thumbnail.resource.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        commandList->ResourceBarrier(1, &toRead);

        PIXEndEvent(commandList);
    });

    if (!executed) {
        return false;
    }
    thumbnail.state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    return true;
}

}  // namespace hm::compositor
