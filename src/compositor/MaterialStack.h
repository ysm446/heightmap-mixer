#pragma once

#include "compositor/MaterialLayer.h"

#include <vector>

namespace mm::compositor {

// レイヤーを下から上へ積んだもの。index 0 が一番下（下地）。
class MaterialStack {
public:
    MaterialStack();

    std::vector<MaterialLayer>& Layers() { return m_layers; }
    const std::vector<MaterialLayer>& Layers() const { return m_layers; }

    MaterialLayer& Add(const MaterialLayer& layer);
    void Remove(size_t index);
    void Move(size_t index, int delta);
    // from の位置のレイヤーを抜いて to の位置へ差し込む。一覧のドラッグ移動で使う。
    void MoveTo(size_t from, size_t to);

    // 変更があったことを記録する。評価器はこれを見て再評価する。
    void MarkDirty() { ++m_revision; }
    uint64_t Revision() const { return m_revision; }

    // 有効なレイヤーのうち一番下のもの。無ければ npos。
    size_t FirstEnabledIndex() const;

private:
    std::vector<MaterialLayer> m_layers;
    uint64_t m_revision = 1;
};

}  // namespace mm::compositor
