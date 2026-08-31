// アンドゥ用の文書スナップショット。写し取り / 書き戻し / 変更の記録と、
// 参照されなくなったペイントマスクの回収。

#include "app/Application.h"

#include "app/ApplicationUiHelpers.h"
#include "core/FileDialog.h"
#include "core/Log.h"
#include "io/ProjectIo.h"
#include "ui/UiStyle.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <DirectXMath.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace mm {

compositor::TextureId Application::ValidTexture(compositor::TextureId id) const {
    return (m_textureLibrary.Find(id) != nullptr) ? id : compositor::kNoTexture;
}

DocumentSnapshot Application::CaptureDocument() const {
    DocumentSnapshot snapshot;
    snapshot.layers = m_materialStack.Layers();
    snapshot.selectedLayer = m_selectedLayer;
    snapshot.selectedMaterial = m_selectedMaterial;

    snapshot.materials.reserve(m_materialLibrary.Entries().size());
    for (const compositor::MaterialAsset& asset : m_materialLibrary.Entries()) {
        MaterialSnapshot material;
        material.id = asset.id;
        material.name = asset.name;
        material.baseColor = asset.baseColor;
        material.normal = asset.normal;
        material.roughness = asset.roughness;
        material.metallic = asset.metallic;
        material.ambientOcclusion = asset.ambientOcclusion;
        material.height = asset.height;
        material.baseColorTint = asset.baseColorTint;
        material.roughnessValue = asset.roughnessValue;
        material.metallicValue = asset.metallicValue;
        material.ambientOcclusionValue = asset.ambientOcclusionValue;
        snapshot.materials.push_back(std::move(material));
    }
    return snapshot;
}

// 写し取った文書を書き戻す。
//
// **参照している ID は、いま実在するものだけ残す。** テクスチャとペイントマスクは
// 履歴の対象外なので、写し取った後に消えていることがある。
// 宙に浮いた ID を残すと、次に同じ番号が払い出されたとき別の画像が現れる。
void Application::ApplyDocument(const DocumentSnapshot& snapshot) {
    // --- マテリアル ---------------------------------------------------------
    // 写し取った時点に無かったものを消す。破棄は GPU 待機を伴う。
    std::vector<compositor::MaterialAssetId> removed;
    for (const compositor::MaterialAsset& asset : m_materialLibrary.Entries()) {
        const bool kept = std::any_of(
            snapshot.materials.begin(), snapshot.materials.end(),
            [&asset](const MaterialSnapshot& m) { return m.id == asset.id; });
        if (!kept) {
            removed.push_back(asset.id);
        }
    }
    for (const compositor::MaterialAssetId id : removed) {
        m_materialLibrary.Remove(m_device, id);
    }

    for (const MaterialSnapshot& material : snapshot.materials) {
        // 消えていれば ID を保ったまま作り直す。残っていれば中身を上書きする。
        compositor::MaterialAsset& asset =
            m_materialLibrary.RestoreAsset(material.id, material.name);
        asset.name = material.name;
        asset.baseColor = ValidTexture(material.baseColor);
        asset.normal = ValidTexture(material.normal);
        asset.roughness = material.roughness;
        asset.metallic = material.metallic;
        asset.ambientOcclusion = material.ambientOcclusion;
        asset.height = material.height;
        asset.roughness.texture = ValidTexture(asset.roughness.texture);
        asset.metallic.texture = ValidTexture(asset.metallic.texture);
        asset.ambientOcclusion.texture = ValidTexture(asset.ambientOcclusion.texture);
        asset.height.texture = ValidTexture(asset.height.texture);
        asset.baseColorTint = material.baseColorTint;
        asset.roughnessValue = material.roughnessValue;
        asset.metallicValue = material.metallicValue;
        asset.ambientOcclusionValue = material.ambientOcclusionValue;
        asset.thumbnailDirty = true;
    }

    // --- レイヤー -----------------------------------------------------------
    std::vector<compositor::MaterialLayer>& layers = m_materialStack.Layers();
    layers = snapshot.layers;
    for (compositor::MaterialLayer& layer : layers) {
        if (m_materialLibrary.Find(layer.material) == nullptr) {
            layer.material = compositor::kNoMaterialAsset;
        }
        layer.mask.texture.texture = ValidTexture(layer.mask.texture.texture);
        if (m_paintMasks.Find(layer.mask.paint) == nullptr) {
            layer.mask.paint = compositor::kNoPaintMask;
        }
    }
    m_materialStack.MarkDirty();

    const auto layerCount = static_cast<int>(layers.size());
    m_selectedLayer = std::clamp(snapshot.selectedLayer, 0, std::max(0, layerCount - 1));
    const auto materialCount = static_cast<int>(m_materialLibrary.Entries().size());
    m_selectedMaterial = std::clamp(snapshot.selectedMaterial, 0, std::max(0, materialCount - 1));
}

void Application::MarkDocumentChanged() {
    m_documentDirty = true;
    m_materialStack.MarkDirty();
}

// 文書からも履歴からも参照されなくなったペイントマスクを破棄する。
//
// レイヤーを消したときにすぐ捨ててしまうと、アンドゥで戻したときに
// 描いた内容が失われる。参照が完全に無くなるまで持っておき、ここで回収する。
void Application::SweepPaintMasks() {
    if (m_paintMasks.Count() == 0) {
        return;
    }

    std::vector<compositor::PaintMaskId> referenced;
    const auto collect = [&referenced](const std::vector<compositor::MaterialLayer>& layers) {
        for (const compositor::MaterialLayer& layer : layers) {
            if (layer.mask.paint != compositor::kNoPaintMask) {
                referenced.push_back(layer.mask.paint);
            }
        }
    };

    collect(m_materialStack.Layers());
    collect(m_committed.layers);
    for (const DocumentSnapshot& snapshot : m_undoHistory.UndoStack()) {
        collect(snapshot.layers);
    }
    for (const DocumentSnapshot& snapshot : m_undoHistory.RedoStack()) {
        collect(snapshot.layers);
    }

    for (const compositor::PaintMaskId id : m_paintMasks.Ids()) {
        if (std::find(referenced.begin(), referenced.end(), id) == referenced.end()) {
            m_paintMasks.Remove(m_device, id);
        }
    }
}

}  // namespace mm
