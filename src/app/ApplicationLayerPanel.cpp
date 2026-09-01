// レイヤーパネル。一覧（並べ替え / 表示切り替え / 削除）と、
// 選択中レイヤーのプロパティ、ペイントマスクの節。

#include "app/Application.h"

#include "app/ApplicationUiHelpers.h"
#include "core/ColorSpace.h"
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

// レイヤーを 1 枚消す。ツールバーのボタンと一覧の削除アイコンの共通の入口。
//
// 入口を分けると、ペイントマスクの後始末のような手当てが片方に付き忘れる。
void Application::RemoveLayer(int index) {
    std::vector<compositor::MaterialLayer>& layers = m_materialStack.Layers();
    const auto layerCount = static_cast<int>(layers.size());
    // 下地が無くなると合成の起点が消えるので、最後の 1 枚は残す。
    if (index < 0 || index >= layerCount || layerCount <= 1) {
        return;
    }

    // **ペイントマスクはここでは捨てない。** アンドゥで戻したときに描いた内容が
    // 失われるため、履歴からも参照されなくなってから SweepPaintMasks() が回収する。
    m_materialStack.Remove(static_cast<size_t>(index));
    MarkDocumentChanged();

    // 消した行より後ろを選んでいたら 1 つ手前へ詰める。
    if (m_selectedLayer > index) {
        --m_selectedLayer;
    }
    m_selectedLayer = std::clamp(m_selectedLayer, 0, static_cast<int>(layers.size()) - 1);
}

// レイヤー一覧。一番上が最前面。ドラッグで並べ替える。
//
// 行の並びは Quixel Mixer に合わせる。
//   目のアイコン / マテリアルのサムネイル / マスクのサムネイル / 名前 / 削除
//
// 行そのものを `Selectable` にし、その上へ部品を重ねる。
// 部品を `SameLine` で横に並べる方式だと、目のアイコンやサムネイルの上では
// 行を選べなくなり、当たり判定に穴が空く。
void Application::DrawLayerList(float height) {
    std::vector<compositor::MaterialLayer>& layers = m_materialStack.Layers();
    const auto layerCount = static_cast<int>(layers.size());

    // ドラッグと削除の結果はループの外で反映する。走査中に並びを変えない。
    int dropFrom = -1;
    int dropTo = -1;
    int deleteIndex = -1;

    const ImGuiStyle& style = ImGui::GetStyle();
    const float thumbnailSize = ui::Scaled(kLayerRowThumbnail);
    const float eyeSize = ui::Scaled(kLayerRowEye);
    const float rowHeight = thumbnailSize + style.FramePadding.y * 2.0f;
    const float gap = style.ItemInnerSpacing.x;
    const float deleteSize = ImGui::GetFrameHeight();

    if (ImGui::BeginChild("layerList", ImVec2(0.0f, height), ImGuiChildFlags_Borders)) {
        for (int i = layerCount - 1; i >= 0; --i) {
            compositor::MaterialLayer& layer = layers[static_cast<size_t>(i)];
            ImGui::PushID(i);

            // 行の背景と選択。この上へ部品を重ねるので AllowOverlap を付ける。
            if (ImGui::Selectable("##row", m_selectedLayer == i,
                                  ImGuiSelectableFlags_AllowOverlap,
                                  ImVec2(0.0f, rowHeight))) {
                m_selectedLayer = i;
            }

            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoHoldToOpenOthers)) {
                ImGui::SetDragDropPayload(kLayerDragDropType, &i, sizeof(int));
                ImGui::TextUnformatted(layer.name.c_str());
                ImGui::EndDragDropSource();
            }
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload =
                        ImGui::AcceptDragDropPayload(kLayerDragDropType);
                    payload != nullptr) {
                    dropFrom = *static_cast<const int*>(payload->Data);
                    dropTo = i;
                }
                ImGui::EndDragDropTarget();
            }

            const ImVec2 rowMin = ImGui::GetItemRectMin();
            const ImVec2 rowMax = ImGui::GetItemRectMax();
            // 部品を重ねるあいだカーソルを動かすので、次の行の位置を控えておく。
            const ImVec2 nextRow = ImGui::GetCursorScreenPos();
            const float centerY = (rowMin.y + rowMax.y) * 0.5f;
            float x = rowMin.x + style.FramePadding.x;

            // --- 目のアイコン --------------------------------------------
            ImGui::SetCursorScreenPos(ImVec2(x, centerY - eyeSize * 0.5f));
            if (ui::EyeToggle("##visible", &layer.enabled, eyeSize)) {
                MarkDocumentChanged();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(layer.enabled ? "このレイヤーを隠す" : "このレイヤーを表示する");
            }
            x += eyeSize + gap;

            // --- マテリアルのサムネイル ------------------------------------
            const compositor::MaterialAsset* material = m_materialLibrary.Find(layer.material);
            ImGui::SetCursorScreenPos(ImVec2(x, centerY - thumbnailSize * 0.5f));
            if (material != nullptr && material->thumbnail.IsValid()) {
                ui::ThumbnailImage(static_cast<ImTextureID>(material->thumbnail.srv.gpu.ptr),
                                   thumbnailSize);
            } else {
                // マテリアルを割り当てていないレイヤーは定数値で塗られる。
                // 何も出さずに空けるより、その色を出すほうが手がかりになる。
                // 保持しているのはリニア値なので、表示するときは sRGB へ直す。
                ui::ColorSwatch(ImVec4(LinearToSrgb(layer.baseColor.x),
                                       LinearToSrgb(layer.baseColor.y),
                                       LinearToSrgb(layer.baseColor.z), 1.0f),
                                thumbnailSize);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("マテリアル: %s",
                                  (material != nullptr) ? material->name.c_str() : "なし");
            }
            x += thumbnailSize + gap;

            // --- マスクのサムネイル ----------------------------------------
            // 合成のついでに焼いたもの。まだ評価していなければ ptr が 0 になり、
            // ThumbnailImage が枠だけを描く。
            const D3D12_GPU_DESCRIPTOR_HANDLE maskHandle =
                m_renderer.Evaluator().MaskThumbnailHandle(static_cast<size_t>(i));
            ImGui::SetCursorScreenPos(ImVec2(x, centerY - thumbnailSize * 0.5f));
            ui::ThumbnailImage(static_cast<ImTextureID>(maskHandle.ptr), thumbnailSize);
            if (ImGui::IsItemHovered()) {
                const bool isBase = (i == 0);
                ImGui::SetTooltip("マスク: %s%s",
                                  kMaskSourceLabels[static_cast<int>(layer.mask.source)],
                                  isBase ? "（下地なので効かない）" : "");
            }
            x += thumbnailSize + gap;

            // --- 名前 ------------------------------------------------------
            // 削除アイコンの手前で切る。長い名前がボタンへ潜り込まないようにする。
            const float nameRight = rowMax.x - deleteSize - gap;
            ImGui::SetCursorScreenPos(ImVec2(x, centerY - ImGui::GetTextLineHeight() * 0.5f));
            ImGui::PushClipRect(ImVec2(x, rowMin.y), ImVec2(nameRight, rowMax.y), true);
            if (layer.enabled) {
                ImGui::TextUnformatted(layer.name.c_str());
            } else {
                // 隠しているレイヤーは名前も落とす。目のアイコンだけだと見落とす。
                ImGui::TextDisabled("%s", layer.name.c_str());
            }
            ImGui::PopClipRect();

            // --- 削除 ------------------------------------------------------
            // 最後の 1 枚は消せない。下地が無くなると合成の起点が消えるため。
            ImGui::SetCursorScreenPos(ImVec2(rowMax.x - deleteSize, centerY - deleteSize * 0.5f));
            ImGui::BeginDisabled(layerCount <= 1);
            // 記号は `×`（U+00D7）。`✕`(U+2715) のような装飾的な字は
            // Yu Gothic に無く、`?` に化ける。
            if (ImGui::Button("×", ImVec2(deleteSize, deleteSize))) {
                deleteIndex = i;
            }
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::SetTooltip((layerCount > 1) ? "このレイヤーを削除"
                                                   : "最後の 1 枚は削除できない");
            }

            ImGui::SetCursorScreenPos(nextRow);
            ImGui::PopID();
        }

        // 行は SetCursorScreenPos で組み立てているので、最後に実体のあるアイテムを
        // 1 つ置いてカーソル位置を確定させる。これが無いと ImGui が
        // 「アイテムを出さずに境界だけ広げた」と判断して警告を出す。
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
    }
    ImGui::EndChild();

    if (dropFrom >= 0 && dropTo >= 0 && dropFrom != dropTo) {
        m_materialStack.MoveTo(static_cast<size_t>(dropFrom), static_cast<size_t>(dropTo));
        m_selectedLayer = dropTo;
        MarkDocumentChanged();
    }
    if (deleteIndex >= 0) {
        RemoveLayer(deleteIndex);
    }
}

void Application::DrawLayerPanel() {
    if (m_focusDefaultTabs > 0) {
        ImGui::SetNextWindowFocus();
    }
    if (!ImGui::Begin("レイヤー")) {
        ImGui::End();
        return;
    }

    std::vector<compositor::MaterialLayer>& layers = m_materialStack.Layers();
    const auto layerCount = static_cast<int>(layers.size());
    m_selectedLayer = std::clamp(m_selectedLayer, 0, (layerCount > 0) ? layerCount - 1 : 0);

    // 「上が一覧、下がプロパティ」の 2 段。一覧と編集を同時に見られるので、
    // レイヤーを行き来しながらの調整がしやすい。
    // **横ではなく縦に割る。** プロパティは「ラベル：値」の行なので幅を要求し、
    // 横に割ると右カラム全体を広く取らないと成立しない。
    float listHeight = ui::Scaled(m_settings.Ui().layerListHeight);
    // 区画の幅は割る前に測る。子ウィンドウを開いた後だと残りが変わる。
    const float paneWidth = ImGui::GetContentRegionAvail().x;
    ImGui::BeginChild("layerListPane", ImVec2(0.0f, listHeight));

    if (ui::Button("追加")) {
        compositor::MaterialLayer layer;
        layer.name = "レイヤー " + std::to_string(layers.size() + 1);
        m_materialStack.Add(layer);
        m_selectedLayer = static_cast<int>(layers.size()) - 1;
        MarkDocumentChanged();
    }
    ImGui::SameLine();
    if (ui::Button("複製") && layerCount > 0) {
        compositor::MaterialLayer copy = layers[static_cast<size_t>(m_selectedLayer)];
        copy.name += " のコピー";
        // ペイントマスクは ID をそのまま持ち越すと 2 枚のレイヤーで同じテクスチャを
        // 共有してしまう。中身ごと別のマスクへ写す。
        if (copy.mask.paint != compositor::kNoPaintMask) {
            copy.mask.paint = m_paintMasks.Duplicate(m_device, copy.mask.paint);
        }
        m_materialStack.Add(copy);
        m_selectedLayer = static_cast<int>(layers.size()) - 1;
        MarkDocumentChanged();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(layerCount <= 1);
    if (ui::Button("削除")) {
        RemoveLayer(m_selectedLayer);
    }
    ImGui::EndDisabled();

    // 区画の高さいっぱいまで伸ばす。下の 1 行はヒント用に空ける。
    DrawLayerList(-ImGui::GetTextLineHeightWithSpacing());
    ui::HintText("上が最前面。ドラッグで並べ替え");
    ImGui::EndChild();

    // 境界をドラッグして一覧側の高さを変えられるようにする。
    // 高さは設定に覚えさせ、起動のたびに戻らないようにする。
    // 保存は掴んでいた手を離したときだけ（ドラッグ中に毎フレーム書かない）。
    const bool released =
        ui::HorizontalSplitter("layerSplitter", &listHeight, ui::Scaled(kLayerListMinHeight),
                               ui::Scaled(kLayerListMaxHeight), paneWidth);
    // 拡大率を掛ける前の値へ戻して持つ。Scaled(1) が現在の拡大率。
    m_settings.Ui().layerListHeight = listHeight / std::max(ui::Scaled(1.0f), 0.01f);
    if (released) {
        m_settings.Save();
    }

    ImGui::BeginChild("layerPropertyPane", ImVec2(0.0f, 0.0f));

    if (layerCount == 0) {
        ImGui::EndChild();
        ImGui::End();
        return;
    }

    compositor::MaterialLayer& layer = layers[static_cast<size_t>(m_selectedLayer)];
    bool changed = false;

    ui::SectionHeader("基本");
    if (ui::BeginPropertyTable("layerBasicRows")) {
        char nameBuffer[128] = {};
        std::snprintf(nameBuffer, sizeof(nameBuffer), "%s", layer.name.c_str());
        if (ui::PropertyTextInput("名前", nameBuffer, sizeof(nameBuffer))) {
            layer.name = nameBuffer;
            // 名前もアンドゥの対象。落とすと、次のアンドゥで改名まで巻き戻る。
            changed = true;
        }
        // マテリアルを割り当てているときは、見た目はマテリアル側の値で決まる。
        // 同じ意味の値を 2 か所に置くと、どちらが効いているのか分からなくなる。
        const bool hasMaterial = (layer.material != compositor::kNoMaterialAsset);
        if (!hasMaterial) {
            changed |= ui::PropertyColorLinear("ベースカラー", &layer.baseColor.x,
                                               &kDefaultLayer.baseColor.x);
            changed |= ui::PropertyFloat("ラフネス", &layer.roughness, 0.0f, 1.0f,
                                         kDefaultLayer.roughness, nullptr, "%.2f");
            changed |= ui::PropertyFloat("メタルネス", &layer.metallic, 0.0f, 1.0f,
                                         kDefaultLayer.metallic, nullptr, "%.2f");
            changed |= ui::PropertyFloat("AO", &layer.ambientOcclusion, 0.0f, 1.0f,
                                         kDefaultLayer.ambientOcclusion, nullptr, "%.2f");
        }
        changed |= ui::PropertyFloat("UV スケール", &layer.uvScale, 0.25f, 16.0f,
                                     kDefaultLayer.uvScale,
                                     "このレイヤーの模様を何回並べるか", "%.2f", 0, 0.25f);
        ui::EndPropertyTable();
    }
    if (layer.material != compositor::kNoMaterialAsset) {
        ui::HintText("色とサーフェスの値はマテリアル側で決まる");
    }

    ui::SectionHeader("ハイト");
    if (ui::BeginPropertyTable("layerHeightRows")) {
        int heightSource = static_cast<int>(layer.heightSource);
        if (ui::PropertyCombo("ソース", &heightSource, kValueSourceLabels,
                              IM_ARRAYSIZE(kValueSourceLabels),
                              static_cast<int>(kDefaultLayer.heightSource))) {
            layer.heightSource = static_cast<compositor::ValueSource>(heightSource);
            changed = true;
        }
        changed |= ui::PropertyFloat("基準の高さ", &layer.heightBase, -2.0f, 2.0f,
                                     kDefaultLayer.heightBase,
                                     "このレイヤーが「溜まる水位」。下地の高さと比べて勝敗が決まる。"
                                     "起伏の強さを変えてもここは動かない",
                                     "%.2f");
        if (layer.heightSource != compositor::ValueSource::Constant) {
            changed |= ui::PropertyFloat("起伏の強さ", &layer.heightGain, 0.0f, 3.0f,
                                         kDefaultLayer.heightGain,
                                         "基準の高さを中心とした凹凸の振れ幅。0 で平らになる",
                                         "%.2f");
        }
        if (layer.heightSource == compositor::ValueSource::Noise) {
            changed |= DrawNoiseRows(layer.heightNoise, kDefaultLayer.heightNoise, false);
        }
        changed |= ui::PropertyFloat("法線の強さ", &layer.normalStrength, 0.0f, 4.0f,
                                     kDefaultLayer.normalStrength,
                                     "ハイトの勾配から作る法線の強さ。0 で平坦", "%.2f");
        ui::EndPropertyTable();
    }

    ui::SectionHeader("マスク");
    if (ui::BeginPropertyTable("layerMaskRows")) {
        int maskSource = static_cast<int>(layer.mask.source);
        if (ui::PropertyCombo("ソース", &maskSource, kMaskSourceLabels,
                              IM_ARRAYSIZE(kMaskSourceLabels),
                              static_cast<int>(kDefaultLayer.mask.source),
                              "マスクは不透明度として高さと同じ土俵で競合する。"
                              "1.0 にすると高さに関係なく全面を覆う")) {
            layer.mask.source = static_cast<compositor::MaskSource>(maskSource);
            changed = true;
        }
        changed |= ui::PropertyFloat("定数", &layer.mask.constant, 0.0f, 1.0f,
                                     kDefaultLayer.mask.constant,
                                     "ソースの値に掛ける係数", "%.2f");

        if (layer.mask.source == compositor::MaskSource::Texture) {
            changed |= DrawMapSlotRow("画像", layer.mask.texture, m_textureLibrary);
        }
        if (layer.mask.source == compositor::MaskSource::Noise) {
            changed |= DrawNoiseRows(layer.mask.noise, kDefaultLayer.mask.noise);
        }
        if (compositor::IsDerivedMaskSource(layer.mask.source)) {
            changed |= ui::PropertyFloat("強調", &layer.mask.derivedScale, 0.0f, 8.0f,
                                         kDefaultLayer.mask.derivedScale,
                                         "下地から作った値の効き方", "%.2f");
        }

        changed |= ui::PropertyFloat("カーブ", &layer.mask.contrast, 0.0f, 4.0f,
                                     kDefaultLayer.mask.contrast,
                                     "1 で線形。大きいほど境界がはっきりする", "%.2f");
        changed |= ui::PropertyFloat("レベル下限", &layer.mask.levelsLow, 0.0f, 1.0f,
                                     kDefaultLayer.mask.levelsLow, nullptr, "%.2f");
        changed |= ui::PropertyFloat("レベル上限", &layer.mask.levelsHigh, 0.0f, 1.0f,
                                     kDefaultLayer.mask.levelsHigh, nullptr, "%.2f");
        changed |= ui::PropertyBool("反転", &layer.mask.invert, kDefaultLayer.mask.invert);
        ui::EndPropertyTable();
    }

    if (m_selectedLayer == 0) {
        ui::HintText("一番下のレイヤーは下地なのでマスクは効かない");
    }
    switch (layer.mask.source) {
        case compositor::MaskSource::Slope:
            ui::HintText("急な面ほど 1 に近づく");
            break;
        case compositor::MaskSource::Curvature:
            ui::HintText("0.5 が平坦。凸で大、凹で小");
            break;
        case compositor::MaskSource::Cavity:
            ui::HintText("窪んでいるほど 1 に近づく");
            break;
        case compositor::MaskSource::Height:
            ui::HintText("下地が高いほど 1 に近づく");
            break;
        default:
            break;
    }

    if (layer.mask.source == compositor::MaskSource::Paint) {
        ui::SectionHeader("ペイント");
        changed |= DrawPaintSection(layer);
    }

    ui::SectionHeader("マテリアル");
    if (ui::BeginPropertyTable("layerMaterialRows")) {
        changed |= DrawMaterialSlotRow("マテリアル", layer.material, m_materialLibrary);
        ui::EndPropertyTable();
    }
    if (const compositor::MaterialAsset* material = m_materialLibrary.Find(layer.material);
        material != nullptr && material->thumbnail.IsValid()) {
        ImGui::Image(static_cast<ImTextureID>(material->thumbnail.srv.gpu.ptr),
                     ImVec2(ui::Scaled(72.0f), ui::Scaled(72.0f)));
    } else {
        ui::HintText("マテリアルパネルで作って割り当てる");
    }

    ui::SectionHeader("合成");
    if (ui::BeginPropertyTable("layerBlendRows")) {
        changed |= ui::PropertyFloat("境界の柔らかさ", &layer.blendRange, 0.0f, 1.0f,
                                     kDefaultLayer.blendRange,
                                     "0 に近いほど硬い置き換えになる", "%.2f");

        ui::PropertyLabel("書き込み", "このレイヤーが書き込むチャンネル");
        for (uint32_t i = 0; i < IM_ARRAYSIZE(kChannelLabels); ++i) {
            bool enabled = (layer.channelMask & (1u << i)) != 0u;
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::Checkbox(kChannelLabels[i], &enabled)) {
                layer.channelMask = enabled ? (layer.channelMask | (1u << i))
                                            : (layer.channelMask & ~(1u << i));
                changed = true;
            }
            ImGui::PopID();
        }
        ui::PropertyEnd();
        ui::EndPropertyTable();
    }

    if (changed) {
        MarkDocumentChanged();
    }

    ImGui::EndChild();
    ImGui::End();
}

bool Application::DrawPaintSection(compositor::MaterialLayer& layer) {
    bool changed = false;

    if (layer.mask.paint == compositor::kNoPaintMask) {
        ui::HintText("このレイヤーにはまだペイントマスクがない");
        if (ui::Button("マスクを作成", ui::kWideButtonWidth)) {
            layer.mask.paint = m_paintMasks.Add(m_device, 0.0f);
            m_paintMode = (layer.mask.paint != compositor::kNoPaintMask);
            changed = true;
        }
        return changed;
    }

    if (ui::BeginPropertyTable("layerPaintRows")) {
        ui::PropertyBool("ペイントモード", &m_paintMode, false,
                         "オンの間、ビューポートのドラッグがブラシになる");
        ui::PropertyFloat("ブラシ半径", &m_brush.radiusPixels, 4.0f, 256.0f,
                          kDefaultBrush.radiusPixels,
                          "画面上の半径。視点や UV スケールを変えても見た目の大きさは変わらない",
                          "%.0f px");
        ui::PropertyFloat("強さ", &m_brush.strength, 0.01f, 1.0f, kDefaultBrush.strength,
                          "1 回の適用で足す量", "%.2f");
        ui::PropertyFloat("減衰", &m_brush.falloff, 0.2f, 8.0f, kDefaultBrush.falloff,
                          "1 で線形。大きいほど中心に集中する", "%.2f");
        ui::PropertyBool("消しゴム", &m_brush.erase, kDefaultBrush.erase,
                         "左右のドラッグの意味を入れ替える");

        ui::PropertyLabelEmpty("paintFill");
        if (ui::Button("全消去")) {
            m_paintMasks.QueueSnapshot(m_device, layer.mask.paint);
            m_paintMasks.QueueFill(layer.mask.paint, 0.0f);
        }
        ImGui::SameLine();
        if (ui::Button("全塗り")) {
            m_paintMasks.QueueSnapshot(m_device, layer.mask.paint);
            m_paintMasks.QueueFill(layer.mask.paint, 1.0f);
        }
        ui::PropertyEnd();

        ui::PropertyLabelEmpty("paintHistory");
        ImGui::BeginDisabled(!m_paintMasks.CanUndo());
        if (ui::Button("アンドゥ")) {
            m_paintMasks.QueueUndo(m_device);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!m_paintMasks.CanRedo());
        if (ui::Button("リドゥ")) {
            m_paintMasks.QueueRedo(m_device);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("(%zu 段)", m_paintMasks.UndoCount());
        ui::PropertyEnd();

        // 解像度はすべてのペイントマスクで共通。
        int resolution = ResolutionIndex(m_paintMasks.RequestedResolution());
        if (ui::PropertyCombo("解像度", &resolution, kResolutionLabels,
                              IM_ARRAYSIZE(kResolutionLabels), 1,
                              "全ペイントマスクを拡大縮小する。履歴は破棄される")) {
            m_paintMasks.RequestResolution(kResolutionValues[resolution]);
        }

        ui::PropertyLabelEmpty("paintDiscard");
        if (ui::Button("マスクを破棄", ui::kWideButtonWidth)) {
            // 実体はここでは消さない。履歴から参照されている間は SweepPaintMasks が
            // 持っておき、アンドゥで戻したときに描いた内容が失われないようにする
            // （RemoveLayer と同じ方針）。
            layer.mask.paint = compositor::kNoPaintMask;
            m_paintMode = false;
            changed = true;
        }
        ui::PropertyEnd();
        ui::EndPropertyTable();
    }

    ui::HintText("左ドラッグで塗る / 右ドラッグで消す / Alt + 左ドラッグで視点を回す");
    return changed;
}

}  // namespace mm
