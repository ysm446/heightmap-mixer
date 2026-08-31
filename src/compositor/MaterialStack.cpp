#include "compositor/MaterialStack.h"

#include <utility>

namespace mm::compositor {

MaterialStack::MaterialStack() {
    // 既定は「岩の隙間に砂が溜まる」構成。ハイトブレンドの効果が一目で分かる。
    //
    // マスクは不透明度として高さと同じ土俵で競合する。
    // 双方のマスクを 0.5 にすると a = 岩の高さ + 0.5、b = 砂の高さ + 0.5 となり、
    // 高さの大小だけで勝敗が決まる。砂の基準高さが「砂が溜まる水位」になる。
    MaterialLayer rock;
    rock.name = "岩";
    rock.baseColor = {0.30f, 0.28f, 0.26f};
    rock.roughness = 0.70f;
    rock.metallic = 0.0f;
    rock.heightSource = ValueSource::Noise;
    rock.heightBase = 0.5f;
    rock.heightGain = 1.0f;
    rock.heightNoise = NoiseParams{NoiseType::Fbm, 7.0f, 1.0f, 6, 0.0f};
    rock.normalStrength = 1.0f;
    rock.mask.source = MaskSource::Constant;
    rock.mask.constant = 1.0f;
    rock.blendRange = 0.2f;
    m_layers.push_back(rock);

    MaterialLayer sand;
    sand.name = "砂";
    sand.baseColor = {0.68f, 0.58f, 0.40f};
    sand.roughness = 0.90f;
    sand.metallic = 0.0f;
    sand.heightSource = ValueSource::Noise;
    // 砂が溜まる水位。岩の高さ（0〜1）の中央より少し下に置く。
    sand.heightBase = 0.445f;
    sand.heightGain = 0.05f;
    sand.heightNoise = NoiseParams{NoiseType::Fbm, 26.0f, 0.05f, 4, 11.0f};
    sand.normalStrength = 0.25f;
    sand.mask.source = MaskSource::Constant;
    sand.mask.constant = 0.5f;
    sand.blendRange = 0.05f;
    m_layers.push_back(sand);

    // 中間結果由来のマスクの例。下地の窪みにだけ苔を生やす。
    MaterialLayer moss;
    moss.name = "苔";
    moss.baseColor = {0.14f, 0.24f, 0.10f};
    moss.roughness = 0.85f;
    moss.metallic = 0.0f;
    moss.heightSource = ValueSource::Noise;
    moss.heightBase = 0.53f;
    moss.heightGain = 0.06f;
    moss.heightNoise = NoiseParams{NoiseType::Worley, 40.0f, 0.06f, 3, 23.0f};
    moss.normalStrength = 0.4f;
    moss.mask.source = MaskSource::Cavity;
    moss.mask.constant = 1.0f;
    moss.mask.derivedScale = 1.0f;
    moss.mask.contrast = 1.8f;
    moss.mask.levelsLow = 0.54f;
    moss.mask.levelsHigh = 0.80f;
    moss.blendRange = 0.08f;
    m_layers.push_back(moss);
}

MaterialLayer& MaterialStack::Add(const MaterialLayer& layer) {
    m_layers.push_back(layer);
    MarkDirty();
    return m_layers.back();
}

void MaterialStack::Remove(size_t index) {
    if (index >= m_layers.size()) {
        return;
    }
    m_layers.erase(m_layers.begin() + static_cast<ptrdiff_t>(index));
    MarkDirty();
}

void MaterialStack::Move(size_t index, int delta) {
    if (index >= m_layers.size() || delta == 0) {
        return;
    }
    const auto target = static_cast<ptrdiff_t>(index) + delta;
    if (target < 0 || target >= static_cast<ptrdiff_t>(m_layers.size())) {
        return;
    }
    std::swap(m_layers[index], m_layers[static_cast<size_t>(target)]);
    MarkDirty();
}

void MaterialStack::MoveTo(size_t from, size_t to) {
    if (from >= m_layers.size() || to >= m_layers.size() || from == to) {
        return;
    }
    // 入れ替えではなく「抜いて差し込む」。間のレイヤーの順序を保つ。
    MaterialLayer moved = std::move(m_layers[from]);
    m_layers.erase(m_layers.begin() + static_cast<ptrdiff_t>(from));
    m_layers.insert(m_layers.begin() + static_cast<ptrdiff_t>(to), std::move(moved));
    MarkDirty();
}

size_t MaterialStack::FirstEnabledIndex() const {
    for (size_t i = 0; i < m_layers.size(); ++i) {
        if (m_layers[i].enabled) {
            return i;
        }
    }
    return static_cast<size_t>(-1);
}

}  // namespace mm::compositor
