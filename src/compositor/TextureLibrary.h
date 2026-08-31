#pragma once

#include "compositor/MaterialLayer.h"
#include "rhi/Device.h"
#include "rhi/PipelineCache.h"

#include <filesystem>
#include <string>
#include <vector>

namespace mm::compositor {

struct LibraryTexture {
    TextureId id = kNoTexture;
    std::string name;
    std::filesystem::path path;
    rhi::GpuTexture texture;
    // 同じリソースに対する 2 つの SRV。用途に応じて使い分ける。
    // float テクスチャ（EXR）は中身がすでにリニアなので、両方とも同じ SRV を指す。
    uint32_t linearSrvIndex = kInvalidTextureIndex;
    uint32_t srgbSrvIndex = kInvalidTextureIndex;
    // 16bit float（EXR 由来）かどうか。破棄のときに SRV を二重解放しないためにも使う。
    bool isFloat = false;
};

// 読み込んだテクスチャを保持し、bindless インデックスを払い出す。
//
// 8bit の画像（PNG / TGA / JPG）は、ベースカラーを sRGB、ラフネスやハイトをリニアとして
// 読む必要があるため、リソースを TYPELESS で作り UNORM と UNORM_SRGB の 2 つの SRV を張る。
//
// EXR は 16bit float のまま持つ。8bit へ落とすとハイトに階段が出る。
// 中身はすでにリニアなので SRV は 1 つでよい。
class TextureLibrary {
public:
    void Destroy(rhi::Device& device);

    // 画像を読み込んでライブラリに追加する。失敗したら kNoTexture を返す。
    TextureId Load(rhi::Device& device, rhi::PipelineCache& pipelineCache,
                   const std::filesystem::path& path);

    void Remove(rhi::Device& device, TextureId id);

    const std::vector<LibraryTexture>& Entries() const { return m_entries; }

    // シェーダへ渡すインデックス。id が無効なら kInvalidTextureIndex。
    uint32_t SrvIndex(TextureId id, bool srgb) const;
    const LibraryTexture* Find(TextureId id) const;

private:
    bool GenerateMips(rhi::Device& device, rhi::PipelineCache& pipelineCache,
                      rhi::GpuTexture& texture);

    std::vector<LibraryTexture> m_entries;
    TextureId m_nextId = 1;
};

}  // namespace mm::compositor
