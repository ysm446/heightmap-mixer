#pragma once

#include "compositor/MaterialLayer.h"
#include "rhi/Device.h"
#include "rhi/PipelineCache.h"

#include <filesystem>
#include <string>
#include <vector>

namespace hm::compositor {

struct LibraryTexture {
    TextureId id = kNoTexture;
    std::string name;
    std::filesystem::path path;
    rhi::GpuTexture texture;
    // 同じリソースに対する 2 つの SRV。用途に応じて使い分ける。
    uint32_t linearSrvIndex = kInvalidTextureIndex;
    uint32_t srgbSrvIndex = kInvalidTextureIndex;
};

// 読み込んだテクスチャを保持し、bindless インデックスを払い出す。
//
// ベースカラーは sRGB、ラフネスやハイトはリニアとして読む必要があるため、
// リソースは TYPELESS で作り、UNORM と UNORM_SRGB の 2 つの SRV を用意する。
// 同じ画像をどちらの用途にも使えるようにするため。
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

}  // namespace hm::compositor
