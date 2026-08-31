#include "rhi/GpuResource.h"

#include "core/Log.h"

namespace hm::rhi {

ResourceAllocator::~ResourceAllocator() {
    Destroy();
}

bool ResourceAllocator::Create(ID3D12Device* device, IDXGIAdapter* adapter,
                               DescriptorHeap* srvHeap) {
    D3D12MA::ALLOCATOR_DESC desc = {};
    desc.pDevice = device;
    desc.pAdapter = adapter;
    desc.Flags = D3D12MA::ALLOCATOR_FLAG_DEFAULT_POOLS_NOT_ZEROED;

    D3D12MA::Allocator* raw = nullptr;
    if (!HM_CHECK_HR(D3D12MA::CreateAllocator(&desc, &raw))) {
        return false;
    }
    m_allocator.Attach(raw);
    m_device = device;
    m_srvHeap = srvHeap;
    return true;
}

void ResourceAllocator::Destroy() {
    m_allocator.Reset();
    m_device = nullptr;
    m_srvHeap = nullptr;
}

bool ResourceAllocator::CreateTexture2D(const TextureDesc& desc, GpuTexture& outTexture) {
    if (!m_allocator) {
        return false;
    }

    D3D12_RESOURCE_DESC resourceDesc = {};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resourceDesc.Width = desc.width;
    resourceDesc.Height = desc.height;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.MipLevels = static_cast<UINT16>(desc.mipLevels);
    resourceDesc.Format = desc.format;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    resourceDesc.Flags = desc.allowUnorderedAccess ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
                                                   : D3D12_RESOURCE_FLAG_NONE;

    D3D12MA::ALLOCATION_DESC allocDesc = {};
    allocDesc.HeapType = D3D12_HEAP_TYPE_DEFAULT;

    D3D12MA::Allocation* allocation = nullptr;
    ID3D12Resource* resource = nullptr;
    if (!HM_CHECK_HR(m_allocator->CreateResource(&allocDesc, &resourceDesc, desc.initialState,
                                                 nullptr, &allocation,
                                                 IID_PPV_ARGS(&resource)))) {
        return false;
    }

    outTexture = GpuTexture{};
    outTexture.allocation.Attach(allocation);
    outTexture.resource.Attach(resource);
    outTexture.width = desc.width;
    outTexture.height = desc.height;
    outTexture.mipLevels = desc.mipLevels;
    outTexture.format = desc.format;
    outTexture.state = desc.initialState;

    if (desc.debugName != nullptr) {
        outTexture.resource->SetName(desc.debugName);
    }

    if (desc.createSrv && m_srvHeap != nullptr) {
        outTexture.srv = m_srvHeap->Allocate();
        if (!outTexture.srv.IsValid()) {
            return false;
        }
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = desc.format;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = desc.mipLevels;
        m_device->CreateShaderResourceView(outTexture.resource.Get(), &srvDesc, outTexture.srv.cpu);
    }

    if (desc.allowUnorderedAccess && m_srvHeap != nullptr) {
        outTexture.uav = m_srvHeap->Allocate();
        if (!outTexture.uav.IsValid()) {
            return false;
        }
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = desc.format;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        m_device->CreateUnorderedAccessView(outTexture.resource.Get(), nullptr, &uavDesc,
                                            outTexture.uav.cpu);
    }

    return true;
}

bool ResourceAllocator::CreateUploadBuffer(uint64_t sizeInBytes, const wchar_t* debugName,
                                           GpuBuffer& outBuffer) {
    if (!m_allocator || sizeInBytes == 0) {
        return false;
    }

    D3D12_RESOURCE_DESC resourceDesc = {};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resourceDesc.Width = sizeInBytes;
    resourceDesc.Height = 1;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.MipLevels = 1;
    resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    D3D12MA::ALLOCATION_DESC allocDesc = {};
    allocDesc.HeapType = D3D12_HEAP_TYPE_UPLOAD;

    D3D12MA::Allocation* allocation = nullptr;
    ID3D12Resource* resource = nullptr;
    if (!HM_CHECK_HR(m_allocator->CreateResource(&allocDesc, &resourceDesc,
                                                 D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                 &allocation, IID_PPV_ARGS(&resource)))) {
        return false;
    }

    outBuffer = GpuBuffer{};
    outBuffer.allocation.Attach(allocation);
    outBuffer.resource.Attach(resource);
    outBuffer.sizeInBytes = sizeInBytes;
    outBuffer.state = D3D12_RESOURCE_STATE_GENERIC_READ;
    if (debugName != nullptr) {
        outBuffer.resource->SetName(debugName);
    }
    return true;
}

void ResourceAllocator::ReleaseDescriptors(GpuTexture& texture) {
    if (m_srvHeap == nullptr) {
        return;
    }
    m_srvHeap->Free(texture.srv);
    m_srvHeap->Free(texture.uav);
    texture.srv = DescriptorHandle{};
    texture.uav = DescriptorHandle{};
}

void ResourceAllocator::ReleaseDescriptors(GpuBuffer& buffer) {
    if (m_srvHeap == nullptr) {
        return;
    }
    m_srvHeap->Free(buffer.srv);
    m_srvHeap->Free(buffer.uav);
    buffer.srv = DescriptorHandle{};
    buffer.uav = DescriptorHandle{};
}

}  // namespace hm::rhi
