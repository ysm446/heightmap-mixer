#include "ui/UiStyle.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <string>

namespace mm::ui {
namespace {

float g_dpiScale = 1.0f;

// グレー基調のパレット。彩度はほぼ持たせず、明度だけで階層を作る。
// 値は sRGB の 8bit を 255 で割ったもの。design-guide.md の表と一致させること。
constexpr ImVec4 Gray(float value, float alpha = 1.0f) {
    return ImVec4(value, value, value, alpha);
}

constexpr ImVec4 kWindowBg = Gray(0.118f);      // #1E1E1E
constexpr ImVec4 kPanelBg = Gray(0.137f);       // #232323
constexpr ImVec4 kTitleBg = Gray(0.094f);       // #181818
constexpr ImVec4 kFrameBg = Gray(0.169f);       // #2B2B2B
constexpr ImVec4 kFrameBgHovered = Gray(0.212f);// #363636
constexpr ImVec4 kFrameBgActive = Gray(0.255f); // #414141
constexpr ImVec4 kHeaderBg = Gray(0.196f);      // #323232
constexpr ImVec4 kHeaderHovered = Gray(0.235f); // #3C3C3C
constexpr ImVec4 kHeaderActive = Gray(0.290f);  // #4A4A4A
constexpr ImVec4 kBorder = Gray(0.204f);        // #343434
constexpr ImVec4 kSeparator = Gray(0.220f);     // #383838
constexpr ImVec4 kGrab = Gray(0.376f);          // #606060
constexpr ImVec4 kGrabActive = Gray(0.510f);    // #828282
constexpr ImVec4 kText = Gray(0.804f);          // #CDCDCD
constexpr ImVec4 kTextDisabled = Gray(0.455f);  // #747474

// 唯一のアクセント。彩度を持たせすぎるとグレー基調が崩れるので、
// わずかに青へ寄せた明るい灰にとどめる。選択・チェック・つまみの強調にだけ使う。
constexpr ImVec4 kAccent = ImVec4(0.588f, 0.639f, 0.678f, 1.0f);  // #96A3AD

// ツールチップの折り返し幅（フォントサイズ比）。
constexpr float kTooltipWrapRatio = 22.0f;

void DrawTooltip(const char* tooltip) {
    if (tooltip == nullptr || tooltip[0] == '\0') {
        return;
    }
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        return;
    }
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * kTooltipWrapRatio);
    ImGui::TextUnformatted(tooltip);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

// 既定値マーカー。既定値と違うときだけ明るく塗り、押すと既定値へ戻す。
//
// 「既定値から変えてあるか」の視覚表現はこの点だけに集約する。
// ラベルの色を変えるといった二重の表現は入れない。
bool ResetDot(bool isDefault, const std::string& defaultText) {
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);

    const float size = ImGui::GetFrameHeight();
    const bool pressed = ImGui::InvisibleButton("##reset", ImVec2(size, size));

    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    const ImVec2 center((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
    const bool hovered = ImGui::IsItemHovered();

    ImU32 color = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    if (!isDefault) {
        color = ImGui::GetColorU32(hovered ? ImGuiCol_Text : ImGuiCol_CheckMark);
    }
    const float radius = std::max(2.0f, size * 0.16f);
    ImGui::GetWindowDrawList()->AddCircleFilled(center, radius, color);

    if (hovered) {
        ImGui::SetTooltip("既定値に戻す\n既定値: %s", defaultText.c_str());
    }
    return pressed;
}

std::string FormatFloat(float value, const char* format) {
    char buffer[64] = {};
    std::snprintf(buffer, sizeof(buffer), format, value);
    return buffer;
}

bool NearlyEqual(float a, float b) {
    return std::fabs(a - b) <= 1e-5f * std::max(1.0f, std::max(std::fabs(a), std::fabs(b)));
}

// スライダーやコンボの幅。値列の残り幅から既定値マーカーぶんを引いて丸める。
float ValueWidth(float minWidth, float maxWidth) {
    const float reserved = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x;
    return std::clamp(ImGui::GetContentRegionAvail().x - reserved, Scaled(minWidth),
                      Scaled(maxWidth));
}

}  // namespace

float Scaled(float value) {
    return value * g_dpiScale;
}

void ApplyTheme(float dpiScale) {
    g_dpiScale = (dpiScale > 0.0f) ? dpiScale : 1.0f;

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();

    // 角丸は控えめに。面の階層は明度で作り、形では作らない。
    style.WindowRounding = 0.0f;
    style.ChildRounding = 2.0f;
    style.FrameRounding = 2.0f;
    style.PopupRounding = 3.0f;
    style.GrabRounding = 2.0f;
    style.TabRounding = 2.0f;
    style.ScrollbarRounding = 2.0f;

    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;

    style.WindowPadding = ImVec2(10.0f, 8.0f);
    style.FramePadding = ImVec2(7.0f, 4.0f);
    style.CellPadding = ImVec2(4.0f, 3.0f);
    style.ItemSpacing = ImVec2(8.0f, 5.0f);
    style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
    style.ScrollbarSize = 12.0f;
    style.GrabMinSize = 9.0f;

    style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
    style.SeparatorTextBorderSize = 1.0f;
    style.SeparatorTextAlign = ImVec2(0.0f, 0.5f);
    style.SeparatorTextPadding = ImVec2(14.0f, 4.0f);

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text] = kText;
    colors[ImGuiCol_TextDisabled] = kTextDisabled;
    colors[ImGuiCol_WindowBg] = kWindowBg;
    colors[ImGuiCol_ChildBg] = kPanelBg;
    colors[ImGuiCol_PopupBg] = kPanelBg;
    colors[ImGuiCol_Border] = kBorder;
    colors[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

    colors[ImGuiCol_FrameBg] = kFrameBg;
    colors[ImGuiCol_FrameBgHovered] = kFrameBgHovered;
    colors[ImGuiCol_FrameBgActive] = kFrameBgActive;

    colors[ImGuiCol_TitleBg] = kTitleBg;
    colors[ImGuiCol_TitleBgActive] = kHeaderBg;
    colors[ImGuiCol_TitleBgCollapsed] = kTitleBg;
    colors[ImGuiCol_MenuBarBg] = kTitleBg;

    colors[ImGuiCol_ScrollbarBg] = kWindowBg;
    colors[ImGuiCol_ScrollbarGrab] = Gray(0.290f);
    colors[ImGuiCol_ScrollbarGrabHovered] = Gray(0.353f);
    colors[ImGuiCol_ScrollbarGrabActive] = Gray(0.424f);

    colors[ImGuiCol_CheckMark] = kAccent;
    colors[ImGuiCol_SliderGrab] = kGrab;
    colors[ImGuiCol_SliderGrabActive] = kGrabActive;

    colors[ImGuiCol_Button] = kFrameBg;
    colors[ImGuiCol_ButtonHovered] = kFrameBgHovered;
    colors[ImGuiCol_ButtonActive] = kFrameBgActive;

    colors[ImGuiCol_Header] = kHeaderBg;
    colors[ImGuiCol_HeaderHovered] = kHeaderHovered;
    colors[ImGuiCol_HeaderActive] = kHeaderActive;

    colors[ImGuiCol_Separator] = kSeparator;
    colors[ImGuiCol_SeparatorHovered] = Gray(0.318f);
    colors[ImGuiCol_SeparatorActive] = kAccent;

    colors[ImGuiCol_ResizeGrip] = Gray(0.255f, 0.6f);
    colors[ImGuiCol_ResizeGripHovered] = Gray(0.353f, 0.8f);
    colors[ImGuiCol_ResizeGripActive] = kAccent;

    colors[ImGuiCol_Tab] = kTitleBg;
    colors[ImGuiCol_TabHovered] = kHeaderHovered;
    colors[ImGuiCol_TabSelected] = kHeaderBg;
    colors[ImGuiCol_TabDimmed] = kTitleBg;
    colors[ImGuiCol_TabDimmedSelected] = kPanelBg;

    colors[ImGuiCol_DockingPreview] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.35f);
    colors[ImGuiCol_DockingEmptyBg] = kWindowBg;

    colors[ImGuiCol_TableHeaderBg] = kHeaderBg;
    colors[ImGuiCol_TableBorderStrong] = kBorder;
    colors[ImGuiCol_TableBorderLight] = kSeparator;
    colors[ImGuiCol_TableRowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    colors[ImGuiCol_TableRowBgAlt] = Gray(1.0f, 0.02f);

    colors[ImGuiCol_TextSelectedBg] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.35f);
    colors[ImGuiCol_NavCursor] = kAccent;
    colors[ImGuiCol_DragDropTarget] = kAccent;
    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.45f);

    colors[ImGuiCol_PlotLines] = kGrab;
    colors[ImGuiCol_PlotLinesHovered] = kAccent;
    colors[ImGuiCol_PlotHistogram] = kGrab;
    colors[ImGuiCol_PlotHistogramHovered] = kAccent;

    // 余白と枠は最後にまとめて DPI へ合わせる。色はスケールの影響を受けない。
    if (g_dpiScale > 1.0f) {
        style.ScaleAllSizes(g_dpiScale);
    }
}

bool BeginPropertyTable(const char* id) {
    if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchProp)) {
        return false;
    }
    ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, Scaled(kLabelColumnWidth));
    ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
    return true;
}

void EndPropertyTable() {
    ImGui::EndTable();
}

void SectionHeader(const char* label) {
    ImGui::SeparatorText(label);
}

void PropertyLabel(const char* label, const char* tooltip) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    // 「パラメータ名：値」と読めるよう、ラベルの末尾にコロンを付ける。
    ImGui::Text("%s：", label);
    DrawTooltip(tooltip);
    ImGui::TableSetColumnIndex(1);
    ImGui::PushID(label);
}

void PropertyLabelEmpty(const char* id) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TableSetColumnIndex(1);
    ImGui::PushID(id);
}

void PropertyEnd() {
    ImGui::PopID();
}

bool PropertyFloat(const char* label, float* value, float minValue, float maxValue,
                   float defaultValue, const char* tooltip, const char* format,
                   ImGuiSliderFlags flags) {
    // 読み込み直後の範囲外値を UI 側で吸収する。
    *value = std::clamp(*value, minValue, maxValue);

    PropertyLabel(label, tooltip);
    ImGui::SetNextItemWidth(ValueWidth(kSliderMinWidth, kSliderMaxWidth));
    bool changed = ImGui::SliderFloat("##value", value, minValue, maxValue, format, flags);

    if (ResetDot(NearlyEqual(*value, defaultValue), FormatFloat(defaultValue, format))) {
        *value = std::clamp(defaultValue, minValue, maxValue);
        changed = true;
    }
    PropertyEnd();
    return changed;
}

bool PropertyInt(const char* label, int* value, int minValue, int maxValue, int defaultValue,
                 const char* tooltip) {
    *value = std::clamp(*value, minValue, maxValue);

    PropertyLabel(label, tooltip);
    ImGui::SetNextItemWidth(ValueWidth(kSliderMinWidth, kSliderMaxWidth));
    bool changed = ImGui::SliderInt("##value", value, minValue, maxValue);

    if (ResetDot(*value == defaultValue, std::to_string(defaultValue))) {
        *value = std::clamp(defaultValue, minValue, maxValue);
        changed = true;
    }
    PropertyEnd();
    return changed;
}

bool PropertyBool(const char* label, bool* value, bool defaultValue, const char* tooltip) {
    PropertyLabel(label, tooltip);
    bool changed = ImGui::Checkbox("##value", value);

    if (ResetDot(*value == defaultValue, defaultValue ? "オン" : "オフ")) {
        *value = defaultValue;
        changed = true;
    }
    PropertyEnd();
    return changed;
}

bool PropertyColor(const char* label, float* rgb, const float* defaultRgb, const char* tooltip) {
    PropertyLabel(label, tooltip);
    ImGui::SetNextItemWidth(ValueWidth(kSliderMinWidth, kSliderMaxWidth));
    bool changed = ImGui::ColorEdit3("##value", rgb,
                                     ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);

    const bool isDefault = NearlyEqual(rgb[0], defaultRgb[0]) &&
                           NearlyEqual(rgb[1], defaultRgb[1]) &&
                           NearlyEqual(rgb[2], defaultRgb[2]);
    char defaultText[64] = {};
    std::snprintf(defaultText, sizeof(defaultText), "%.2f, %.2f, %.2f", defaultRgb[0],
                  defaultRgb[1], defaultRgb[2]);
    if (ResetDot(isDefault, defaultText)) {
        rgb[0] = defaultRgb[0];
        rgb[1] = defaultRgb[1];
        rgb[2] = defaultRgb[2];
        changed = true;
    }
    PropertyEnd();
    return changed;
}

bool PropertyCombo(const char* label, int* value, const char* const items[], int itemCount,
                   int defaultValue, const char* tooltip) {
    if (itemCount <= 0) {
        return false;
    }
    *value = std::clamp(*value, 0, itemCount - 1);

    PropertyLabel(label, tooltip);
    ImGui::SetNextItemWidth(ValueWidth(kSliderMinWidth, kComboMaxWidth));
    bool changed = ImGui::Combo("##value", value, items, itemCount);

    if (ResetDot(*value == defaultValue, items[std::clamp(defaultValue, 0, itemCount - 1)])) {
        *value = std::clamp(defaultValue, 0, itemCount - 1);
        changed = true;
    }
    PropertyEnd();
    return changed;
}

bool PropertyTextInput(const char* label, char* buffer, size_t bufferSize, const char* tooltip) {
    PropertyLabel(label, tooltip);
    ImGui::SetNextItemWidth(
        std::min(Scaled(kTextInputWidth), ImGui::GetContentRegionAvail().x));
    const bool changed = ImGui::InputText("##value", buffer, bufferSize);
    PropertyEnd();
    return changed;
}

void PropertyValue(const char* label, const char* format, ...) {
    PropertyLabel(label, nullptr);
    va_list args;
    va_start(args, format);
    ImGui::TextV(format, args);
    va_end(args);
    PropertyEnd();
}

bool Button(const char* label, float width) {
    return ImGui::Button(label, ImVec2(Scaled(width), 0.0f));
}

void HintText(const char* format, ...) {
    va_list args;
    va_start(args, format);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextV(format, args);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
    va_end(args);
}

}  // namespace mm::ui
