#include "compositor/TextureLibrary.h"

#include "core/ImageIo.h"
#include "core/Log.h"

#include <pix3.h>

#include <DirectXPackedVector.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>
#include <vector>

namespace hm::compositor {
namespace {

constexpr uint32_t kGroupSize = 8;

uint32_t MipCountFor(uint32_t width, uint32_t height) {
    uint32_t size = std::max(width, height);
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

bool IsExrPath(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension == ".exr";
}

// EXR の float RGBA を half RGBA へ詰め直す。
// 8bit だとハイトの階段が見えるので、EXR は 16bit float のまま持つ。
std::vector<uint8_t> ConvertToHalf4(const HdrImage& image) {
    const size_t texelCount = static_cast<size_t>(image.width) * image.height;
    std::vector<uint8_t> packed(texelCount * 4 * sizeof(uint16_t));
    auto* destination = reinterpret_cast<DirectX::PackedVector::HALF*>(packed.data());
    for (size_t i = 0; i < texelCount * 4; ++i) {
        destination[i] = DirectX::PackedVector::XMConvertFloatToHalf(image.pixels[i]);
    }
    return packed;
}

void TransitionMip(ID3D12GraphicsCommandList* commandList, const rhi::GpuTexture& texture,
                   uint32_t mip, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
    const auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        texture.resource.Get(), before, after, texture.SubresourceIndex(mip, 0));
    commandList->ResourceBarrier(1, &barrier);
}

}  // namespace

void TextureLibrary::Destroy(rhi::Device& device) {
    for (LibraryTexture& entry : m_entries) {
        // float は sRGB 用の SRV を別に張っていない（linear と同じものを指す）。
        // 二重解放しないよう、別に張ったときだけ返す。
        if (!entry.isFloat && entry.srgbSrvIndex != kInvalidTextureIndex) {
            device.SrvHeap().Free(device.SrvHeap().At(entry.srgbSrvIndex));
        }
        device.Allocator().ReleaseDescriptors(entry.texture);
        device.Defer(entry.texture.resource);
        device.Defer(entry.texture.allocation);
    }
    m_entries.clear();
}

const LibraryTexture* TextureLibrary::Find(TextureId id) const {
    if (id == kNoTexture) {
        return nullptr;
    }
    const auto it = std::find_if(m_entries.begin(), m_entries.end(),
                                 [id](const LibraryTexture& entry) { return entry.id == id; });
    return (it != m_entries.end()) ? &(*it) : nullptr;
}

uint32_t TextureLibrary::SrvIndex(TextureId id, bool srgb) const {
    const LibraryTexture* entry = Find(id);
    if (entry == nullptr) {
        return kInvalidTextureIndex;
    }
    return srgb ? entry->srgbSrvIndex : entry->linearSrvIndex;
}

void TextureLibrary::Remove(rhi::Device& device, TextureId id) {
    const auto it = std::find_if(m_entries.begin(), m_entries.end(),
                                 [id](const LibraryTexture& entry) { return entry.id == id; });
    if (it == m_entries.end()) {
        return;
    }

    // Destroy と同じ理由で、別に張ったときだけ返す。
    if (!it->isFloat && it->srgbSrvIndex != kInvalidTextureIndex) {
        device.SrvHeap().Free(device.SrvHeap().At(it->srgbSrvIndex));
    }
    device.Allocator().ReleaseDescriptors(it->texture);
    device.Defer(it->texture.resource);
    device.Defer(it->texture.allocation);
    m_entries.erase(it);
}

TextureId TextureLibrary::Load(rhi::Device& device, rhi::PipelineCache& pipelineCache,
                               const std::filesystem::path& path) {
    // EXR は 16bit float のまま持つ。8bit へ落とすとハイトに階段が出る。
    // それ以外（PNG / TGA / JPG）は 8bit で読む。
    const bool isFloat = IsExrPath(path);

    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> pixels;  // 出力フォーマットへ詰め直したもの
    size_t sourceRowPitch = 0;

    if (isFloat) {
        HdrImage image;
        if (!LoadExrImage(path, image)) {
            return kNoTexture;
        }
        width = image.width;
        height = image.height;
        pixels = ConvertToHalf4(image);
        sourceRowPitch = static_cast<size_t>(width) * 4 * sizeof(uint16_t);
    } else {
        LdrImage image;
        if (!LoadLdrImage(path, image)) {
            return kNoTexture;
        }
        width = image.width;
        height = image.height;
        pixels = std::move(image.pixels);
        sourceRowPitch = static_cast<size_t>(width) * 4;
    }

    LibraryTexture entry;
    entry.path = path;
    entry.name = path.filename().string();
    entry.isFloat = isFloat;

    rhi::TextureDesc desc;
    desc.width = width;
    desc.height = height;
    desc.mipLevels = MipCountFor(width, height);
    if (isFloat) {
        // float は 1 つのビューで足りる。中身はすでにリニア。
        desc.format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    } else {
        // sRGB / リニアの両方の SRV を張れるよう TYPELESS で作る。
        desc.format = DXGI_FORMAT_R8G8B8A8_TYPELESS;
        desc.srvFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.uavFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    }
    desc.allowUnorderedAccess = true;
    desc.createMipUavs = true;
    desc.createMipSrvs = true;
    desc.createSrv = true;
    desc.initialState = D3D12_RESOURCE_STATE_COPY_DEST;
    desc.debugName = L"LibraryTexture";
    if (!device.Allocator().CreateTexture2D(desc, entry.texture)) {
        return kNoTexture;
    }
    entry.linearSrvIndex = entry.texture.SrvIndex();

    if (isFloat) {
        // EXR の中身はリニアなので、sRGB として読む必要がない。同じ SRV を指す。
        entry.srgbSrvIndex = entry.linearSrvIndex;
    } else {
        // sRGB 用の SRV を追加で張る。
        const rhi::DescriptorHandle srgbHandle = device.SrvHeap().Allocate();
        if (!srgbHandle.IsValid()) {
            return kNoTexture;
        }
        D3D12_SHADER_RESOURCE_VIEW_DESC srgbSrvDesc = {};
        srgbSrvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        srgbSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srgbSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srgbSrvDesc.Texture2D.MipLevels = desc.mipLevels;
        device.GetDevice()->CreateShaderResourceView(entry.texture.resource.Get(), &srgbSrvDesc,
                                                     srgbHandle.cpu);
        entry.srgbSrvIndex = srgbHandle.index;
    }

    // ミップ 0 をアップロードする。
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
    UINT rowCount = 0;
    UINT64 rowSizeInBytes = 0;
    UINT64 totalBytes = 0;
    const D3D12_RESOURCE_DESC resourceDesc = entry.texture.resource->GetDesc();
    device.GetDevice()->GetCopyableFootprints(&resourceDesc, 0, 1, 0, &footprint, &rowCount,
                                              &rowSizeInBytes, &totalBytes);

    rhi::GpuBuffer staging;
    if (!device.Allocator().CreateUploadBuffer(totalBytes, L"TextureStaging", staging)) {
        return kNoTexture;
    }

    void* mapped = nullptr;
    const D3D12_RANGE readRange = {0, 0};
    if (!HM_CHECK_HR(staging.resource->Map(0, &readRange, &mapped))) {
        return kNoTexture;
    }
    auto* destination = static_cast<uint8_t*>(mapped) + footprint.Offset;
    for (uint32_t row = 0; row < rowCount; ++row) {
        std::memcpy(destination + static_cast<size_t>(row) * footprint.Footprint.RowPitch,
                    pixels.data() + static_cast<size_t>(row) * sourceRowPitch,
                    static_cast<size_t>(rowSizeInBytes));
    }
    staging.resource->Unmap(0, nullptr);

    const bool uploaded = device.ExecuteImmediate([&](ID3D12GraphicsCommandList* commandList) {
        PIXBeginEvent(commandList, PIX_COLOR(160, 200, 120), "UploadTexture");
        const CD3DX12_TEXTURE_COPY_LOCATION destinationLocation(entry.texture.resource.Get(), 0);
        const CD3DX12_TEXTURE_COPY_LOCATION sourceLocation(staging.resource.Get(), footprint);
        commandList->CopyTextureRegion(&destinationLocation, 0, 0, 0, &sourceLocation, nullptr);
        PIXEndEvent(commandList);
    });
    device.Defer(staging.resource);
    device.Defer(staging.allocation);
    if (!uploaded) {
        return kNoTexture;
    }

    // アップロード直後はミップ 0 のみ COPY_DEST。残りは作成時の状態のまま。
    entry.texture.state = D3D12_RESOURCE_STATE_COPY_DEST;
    if (!GenerateMips(device, pipelineCache, entry.texture)) {
        return kNoTexture;
    }

    entry.id = m_nextId++;
    m_entries.push_back(std::move(entry));
    return m_entries.back().id;
}

bool TextureLibrary::GenerateMips(rhi::Device& device, rhi::PipelineCache& pipelineCache,
                                  rhi::GpuTexture& texture) {
    ID3D12PipelineState* pipeline = pipelineCache.GetCompute(L"TextureMips.hlsl", L"CsMain");
    if (pipeline == nullptr) {
        return false;
    }

    constexpr D3D12_RESOURCE_STATES kReadState =
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

    struct MipConstants {
        uint32_t sourceIndex;
        uint32_t outputIndex;
        uint32_t outputWidth;
        uint32_t outputHeight;
    };

    const bool executed = device.ExecuteImmediate([&](ID3D12GraphicsCommandList* commandList) {
        PIXBeginEvent(commandList, PIX_COLOR(160, 200, 120), "TextureMips");
        commandList->SetComputeRootSignature(pipelineCache.GlobalRootSignature());
        commandList->SetPipelineState(pipeline);

        uint32_t width = texture.width;
        uint32_t height = texture.height;

        for (uint32_t mip = 1; mip < texture.mipLevels; ++mip) {
            // 参照元ミップを読み取り状態へ。ミップ 0 だけは COPY_DEST から遷移する。
            TransitionMip(commandList, texture, mip - 1,
                          (mip == 1) ? D3D12_RESOURCE_STATE_COPY_DEST
                                     : D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                          kReadState);

            width = std::max<uint32_t>(width >> 1, 1);
            height = std::max<uint32_t>(height >> 1, 1);

            const MipConstants constants{texture.MipSrvIndex(mip - 1), texture.MipUavIndex(mip),
                                         width, height};
            commandList->SetComputeRoot32BitConstants(0, sizeof(constants) / sizeof(uint32_t),
                                                      &constants, 0);
            commandList->Dispatch(DispatchCount(width), DispatchCount(height), 1);

            const auto barrier = CD3DX12_RESOURCE_BARRIER::UAV(texture.resource.Get());
            commandList->ResourceBarrier(1, &barrier);
        }

        // 最後のミップも読み取り状態へ移し、リソース全体を揃える。
        const uint32_t lastMip = texture.mipLevels - 1;
        TransitionMip(commandList, texture, lastMip,
                      (texture.mipLevels == 1) ? D3D12_RESOURCE_STATE_COPY_DEST
                                               : D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                      kReadState);
        PIXEndEvent(commandList);
    });

    if (executed) {
        texture.state = kReadState;
    }
    return executed;
}

}  // namespace hm::compositor
