#include "rhi/UploadRing.h"

#include "core/Log.h"

namespace hm::rhi {
namespace {

uint64_t AlignUp(uint64_t value, uint64_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

}  // namespace

bool UploadRing::Create(ResourceAllocator& allocator, uint64_t bytesPerFrame) {
    m_bytesPerFrame = AlignUp(bytesPerFrame, 256);

    const uint64_t total = m_bytesPerFrame * kFrameCount;
    if (!allocator.CreateUploadBuffer(total, L"UploadRing", m_buffer)) {
        return false;
    }

    // 常時マップしたままにする。アップロードヒープなので Unmap は不要。
    void* mapped = nullptr;
    const D3D12_RANGE readRange = {0, 0};
    if (!HM_CHECK_HR(m_buffer.resource->Map(0, &readRange, &mapped))) {
        return false;
    }
    m_mapped = static_cast<uint8_t*>(mapped);

    HM_LOG_INFO("アップロードリング: %llu MB (%llu MB x %u フレーム)",
                static_cast<unsigned long long>(total / (1024 * 1024)),
                static_cast<unsigned long long>(m_bytesPerFrame / (1024 * 1024)), kFrameCount);
    return true;
}

void UploadRing::Destroy() {
    m_mapped = nullptr;
    m_buffer = GpuBuffer{};
    m_bytesPerFrame = 0;
    m_frameBase = 0;
    m_offset = 0;
}

void UploadRing::BeginFrame(uint32_t frameIndex) {
    m_frameBase = m_bytesPerFrame * frameIndex;
    m_offset = 0;
    m_overflowReported = false;
}

UploadAllocation UploadRing::Allocate(uint64_t size, uint64_t alignment) {
    UploadAllocation result;
    if (m_mapped == nullptr || size == 0) {
        return result;
    }

    const uint64_t alignedOffset = AlignUp(m_offset, alignment);
    if (alignedOffset + size > m_bytesPerFrame) {
        if (!m_overflowReported) {
            HM_LOG_ERROR("アップロードリングが不足しました (要求 %llu, 残り %llu)",
                         static_cast<unsigned long long>(size),
                         static_cast<unsigned long long>(m_bytesPerFrame - alignedOffset));
            m_overflowReported = true;
        }
        return result;
    }

    const uint64_t absolute = m_frameBase + alignedOffset;
    result.cpu = m_mapped + absolute;
    result.gpuAddress = m_buffer.GpuAddress() + absolute;
    result.resource = m_buffer.resource.Get();
    result.offset = absolute;
    result.size = size;

    m_offset = alignedOffset + size;
    if (m_offset > m_peakBytes) {
        m_peakBytes = m_offset;
    }
    return result;
}

}  // namespace hm::rhi
