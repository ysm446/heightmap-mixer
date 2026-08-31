#pragma once

// D3D12MemAlloc.h は <d3d12.h> を取り込むため、必ず先に DirectX-Headers 版を通す。
#include "rhi/Common.h"
#include "rhi/DescriptorHeap.h"

#include <D3D12MemAlloc.h>

#include <string>

namespace hm::rhi {

// リソースの状態は当面サブリソース単位ではなく、リソース全体で 1 つだけ持つ。
// ミップ単位で別状態にしたくなった時点で拡張する。
struct GpuBuffer {
    ComPtr<D3D12MA::Allocation> allocation;
    ComPtr<ID3D12Resource> resource;
    uint64_t sizeInBytes = 0;
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
    DescriptorHandle srv;
    DescriptorHandle uav;

    bool IsValid() const { return resource != nullptr; }
    D3D12_GPU_VIRTUAL_ADDRESS GpuAddress() const {
        return resource ? resource->GetGPUVirtualAddress() : 0;
    }
};

struct GpuTexture {
    ComPtr<D3D12MA::Allocation> allocation;
    ComPtr<ID3D12Resource> resource;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t mipLevels = 1;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
    DescriptorHandle srv;
    DescriptorHandle uav;

    bool IsValid() const { return resource != nullptr; }

    // bindless 用のインデックス。シェーダへはこの値を渡す。
    uint32_t SrvIndex() const { return srv.index; }
    uint32_t UavIndex() const { return uav.index; }
};

struct TextureDesc {
    uint32_t width = 1;
    uint32_t height = 1;
    uint32_t mipLevels = 1;
    DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;
    bool allowUnorderedAccess = false;
    bool createSrv = true;
    D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COMMON;
    const wchar_t* debugName = nullptr;
};

// D3D12MemoryAllocator を包み、リソース生成とディスクリプタ確保をまとめて行う。
class ResourceAllocator {
public:
    ResourceAllocator() = default;
    ~ResourceAllocator();

    ResourceAllocator(const ResourceAllocator&) = delete;
    ResourceAllocator& operator=(const ResourceAllocator&) = delete;

    bool Create(ID3D12Device* device, IDXGIAdapter* adapter, DescriptorHeap* srvHeap);
    void Destroy();

    bool CreateTexture2D(const TextureDesc& desc, GpuTexture& outTexture);

    // アップロードヒープ上のバッファ。CPU から直接書き込む用途に使う。
    bool CreateUploadBuffer(uint64_t sizeInBytes, const wchar_t* debugName, GpuBuffer& outBuffer);

    // ディスクリプタを解放してから、リソース本体を呼び出し側の削除キューへ渡せる状態にする。
    void ReleaseDescriptors(GpuTexture& texture);
    void ReleaseDescriptors(GpuBuffer& buffer);

    D3D12MA::Allocator* Raw() const { return m_allocator.Get(); }

private:
    ComPtr<D3D12MA::Allocator> m_allocator;
    ID3D12Device* m_device = nullptr;
    DescriptorHeap* m_srvHeap = nullptr;
};

}  // namespace hm::rhi
