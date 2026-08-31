#include "rhi/DeletionQueue.h"

#include <algorithm>

namespace mm::rhi {

void DeletionQueue::Push(ComPtr<IUnknown> object, uint64_t fenceValue) {
    if (!object) {
        return;
    }
    m_entries.push_back(Entry{fenceValue, std::move(object)});
}

void DeletionQueue::Collect(uint64_t completedFenceValue) {
    if (m_entries.empty()) {
        return;
    }
    const auto removed = std::remove_if(m_entries.begin(), m_entries.end(),
                                        [completedFenceValue](const Entry& entry) {
                                            return entry.fenceValue <= completedFenceValue;
                                        });
    m_entries.erase(removed, m_entries.end());
}

void DeletionQueue::Flush() {
    m_entries.clear();
}

}  // namespace mm::rhi
