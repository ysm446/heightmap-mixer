#pragma once

#include "compositor/MaterialLayer.h"
#include "compositor/TextureLibrary.h"
#include "rhi/Device.h"
#include "rhi/PipelineCache.h"

#include <string>
#include <vector>

namespace hm::compositor {

// マテリアル 1 つぶん。PBR のマップ一式に名前を付けたもの。
//
// レイヤーはマテリアルを 1 つ参照する（Quixel Mixer と同じ形）。
// マップを個別に差し替えるのではなく、マテリアルを差し替えることで見た目を変える。
// マスクだけはレイヤー固有なので、ここには入れない。
struct MaterialAsset {
    MaterialAssetId id = kNoMaterialAsset;
    std::string name;

    // 未指定のスロットは下の定数を使う。
    TextureId baseColor = kNoTexture;         // sRGB として読む
    TextureId normal = kNoTexture;            // タンジェント空間法線（リニア）
    TextureId roughness = kNoTexture;         // R チャンネル
    TextureId metallic = kNoTexture;          // R チャンネル
    TextureId ambientOcclusion = kNoTexture;  // R チャンネル
    TextureId height = kNoTexture;            // R チャンネル

    DirectX::XMFLOAT3 baseColorTint = {0.5f, 0.5f, 0.5f};
    float roughnessValue = 0.5f;
    float metallicValue = 0.0f;
    float ambientOcclusionValue = 1.0f;

    // 一覧に出すサムネイル。マップかパラメータを変えたら作り直す。
    rhi::GpuTexture thumbnail;
    bool thumbnailDirty = true;
};

// マテリアルを保持し、サムネイルを作る。
class MaterialLibrary {
public:
    void Destroy(rhi::Device& device);

    MaterialAssetId Add(const std::string& name);
    MaterialAssetId Duplicate(const MaterialAsset& source);
    void Remove(rhi::Device& device, MaterialAssetId id);
    void Clear(rhi::Device& device);

    const std::vector<MaterialAsset>& Entries() const { return m_entries; }
    const MaterialAsset* Find(MaterialAssetId id) const;
    MaterialAsset* FindMutable(MaterialAssetId id);

    // サムネイルの作り直しを予約する。マップやパラメータを変えたら呼ぶ。
    void MarkThumbnailDirty(MaterialAssetId id);

    // 予約されたサムネイルを作る。GPU 待機を伴うため、フレームの外で呼ぶ。
    void ProcessPendingWork(rhi::Device& device, rhi::PipelineCache& pipelineCache,
                            const TextureLibrary& textures);

    // 一覧で使うサムネイルのハンドル。まだ無ければ ptr が 0。
    D3D12_GPU_DESCRIPTOR_HANDLE ThumbnailHandle(MaterialAssetId id) const;

private:
    bool BuildThumbnail(rhi::Device& device, rhi::PipelineCache& pipelineCache,
                        const TextureLibrary& textures, MaterialAsset& asset);

    std::vector<MaterialAsset> m_entries;
    MaterialAssetId m_nextId = 1;
};

}  // namespace hm::compositor
