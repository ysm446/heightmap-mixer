#pragma once

#include <imgui.h>

#include <cstddef>

// UI の見た目とプロパティ行の共通部品。
//
// **UI を変更するときは docs/design/design-guide.md に従うこと。**
// 個々のパネルが ImGui のウィジェットを直接呼ぶのではなく、
// ここのヘルパーを通すことで、ラベルの体裁・幅・既定値・ツールチップが揃う。
namespace mm::ui {

// 部品の寸法。96 DPI 基準の値を置き、使うときに Scaled() で現在の DPI へ合わせる。
// 値の意味と使い分けは design-guide.md にある。種類を勝手に増やさない。
inline constexpr float kLabelColumnWidth = 108.0f;
inline constexpr float kSliderMinWidth = 76.0f;
inline constexpr float kSliderMaxWidth = 176.0f;
inline constexpr float kComboMaxWidth = 190.0f;
inline constexpr float kButtonWidth = 68.0f;
inline constexpr float kWideButtonWidth = 148.0f;
inline constexpr float kTextInputWidth = 190.0f;

// グレー基調のテーマを適用する。ImGui のコンテキストを作った直後に 1 回だけ呼ぶ。
void ApplyTheme(float dpiScale);

// 96 DPI 基準の寸法を現在の DPI へ合わせる。
float Scaled(float value);

// --- プロパティ行 ---------------------------------------------------------
//
// すべての設定値は「ラベル：ウィジェット」の 2 列テーブルの 1 行として描く。
// 左列にラベル（末尾に全角コロン）、右列にウィジェットを置く。

bool BeginPropertyTable(const char* id);
void EndPropertyTable();

// セクション見出し。プロパティテーブルの外で呼ぶ。
void SectionHeader(const char* label);

// 行を開き、ラベル列を描いて値列へ移動する。既製の行で表せないウィジェットを
// 自分で置きたいときに使う。ID を積むので、行の終わりで PropertyEnd() を呼ぶこと。
void PropertyLabel(const char* label, const char* tooltip = nullptr);
// ラベルを置かない行。ボタンだけを値列に並べたいときに使う。id は行ごとに固有にする。
void PropertyLabelEmpty(const char* id);
void PropertyEnd();

// snapStep を渡すと、**ドラッグ中だけ**その刻みへ吸着する。
// 「UV スケールを 2.00 にしたい」のように、きりのいい値を狙う行で使う。
// Ctrl + クリックの直接入力は丸めない（狙って入れた値を動かさないため）。
bool PropertyFloat(const char* label, float* value, float minValue, float maxValue,
                   float defaultValue, const char* tooltip = nullptr,
                   const char* format = "%.3f", ImGuiSliderFlags flags = 0,
                   float snapStep = 0.0f);
bool PropertyInt(const char* label, int* value, int minValue, int maxValue, int defaultValue,
                 const char* tooltip = nullptr);
bool PropertyBool(const char* label, bool* value, bool defaultValue,
                  const char* tooltip = nullptr);
bool PropertyColor(const char* label, float* rgb, const float* defaultRgb,
                   const char* tooltip = nullptr);
// items は要素数 itemCount の配列。ImGui の "A\0B\0" 形式ではなく配列で受ける。
bool PropertyCombo(const char* label, int* value, const char* const items[], int itemCount,
                   int defaultValue, const char* tooltip = nullptr);
bool PropertyTextInput(const char* label, char* buffer, size_t bufferSize,
                       const char* tooltip = nullptr);
// 表示専用の値。
void PropertyValue(const char* label, const char* format, ...);

// 値列いっぱいに広げないボタン。幅は kButtonWidth / kWideButtonWidth のどちらか。
bool Button(const char* label, float width = kButtonWidth);

// --- サムネイル一覧 -------------------------------------------------------

// サムネイル 1 枚ぶんの選択枠。**画像を描いた後に呼ぶこと。**
//
// 選択は「背景を敷く」では表せない。サムネイルが不透明だと下の色が完全に隠れる。
// 画像の上に枠を重ねて描く。
//
// 呼ぶ前に ImGui::Image / Button を置き、その矩形（GetItemRectMin / Max）を渡す。
void ThumbnailFrame(const ImVec2& min, const ImVec2& max, bool selected, bool hovered);

// 通知の意味色。ステータスバーで警告とエラーを区別するためだけに使う。
// グレー基調を崩さないよう彩度は低く抑えてある。配色の一部なので UiStyle.cpp に置く。
ImU32 WarnColor();
ImU32 ErrorColor();

// 補助テキスト。操作の説明や単位の目安を 1 行で添えるときに使う。
void HintText(const char* format, ...);

}  // namespace mm::ui
