// UI の相互作用のテスト。GPU もウィンドウも要らない。
//
// ImGui へマウスイベントを直接注入し、1 フレームずつ進めて結果を見る。
// **スクリーンショットでは確認できない操作**（ドラッグ、ホバー）を押さえるためのもの。
//
// ここで守りたいのは次の 2 つ。どちらも壊れても画面には何も出ないので、
// 手で気づくのが難しい。
//
//   1. テクスチャのサムネイルからマテリアルのマップ欄へドラッグして割り当てられる
//   2. マップ欄のツールチップを足しても、1 が壊れない

#include "ui/UiStyle.h"

#include "TestSupport.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <cstdio>
#include <cstring>

namespace {

// アプリと同じペイロード種別（Application.cpp の kTextureDragDropType）。
const char* const kPayloadType = "MM_TEXTURE";

const ImVec2 kSourcePos(50.0f, 50.0f);
const ImVec2 kTargetPos(50.0f, 300.0f);
constexpr float kThumbnailSize = 72.0f;

using mm::tests::Check;
using mm::tests::Section;

void Frame(float x, float y, bool down) {
    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent(x, y);
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, down);
    ImGui::NewFrame();
}

// ツールチップのウィンドウが実際に出ているか。
bool TooltipVisible() {
    const ImGuiContext& g = *ImGui::GetCurrentContext();
    for (const ImGuiWindow* window : g.Windows) {
        if (window->Active && std::strstr(window->Name, "Tooltip") != nullptr) {
            return true;
        }
    }
    return false;
}

struct Result {
    bool sourceStarted = false;
    bool dropped = false;
    bool tooltipShown = false;
};

// ドラッグ元。アプリのテクスチャ一覧と同じ形。
//
// **「ImGui::Image を使うと動かない」ことはここでは試せない。**
// ID を持たないアイテムを掴むと ImGui が IM_ASSERT(0) で止まるようになっており
// （imgui.cpp の BeginDragDropSource）、Debug ビルドではテストごと落ちる。
// 裏を返せば、実装を元に戻してしまってもアプリの Debug ビルドが即座に知らせてくれる。
void SubmitSource(Result& result) {
    ImGui::SetCursorScreenPos(kSourcePos);
    ImGui::PushID(1234);
    mm::ui::ThumbnailButton("##thumbnail", ImTextureID{}, kThumbnailSize, true);
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoHoldToOpenOthers)) {
        result.sourceStarted = true;
        const unsigned int textureId = 7;
        ImGui::SetDragDropPayload(kPayloadType, &textureId, sizeof(textureId));
        ImGui::TextUnformatted("dragging");
        ImGui::EndDragDropSource();
    }
    ImGui::PopID();
}

// 落とす先。アプリの DrawTextureCombo と同じ順序で積む。
// コンボ → ドロップの受け口 → ツールチップ。この順序が要点。
void SubmitTarget(Result& result, bool withTooltip) {
    ImGui::SetCursorScreenPos(kTargetPos);
    ImGui::SetNextItemWidth(190.0f);
    if (ImGui::BeginCombo("##value", "T_Dusty_Gravel_Ground_vd3odhr_2K_D.EXR")) {
        ImGui::EndCombo();
    }
    if (ImGui::BeginDragDropTarget()) {
        if (ImGui::AcceptDragDropPayload(kPayloadType) != nullptr) {
            result.dropped = true;
        }
        ImGui::EndDragDropTarget();
    }
    if (withTooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort) &&
        ImGui::GetDragDropPayload() == nullptr) {
        ImGui::SetTooltip("T_Dusty_Gravel_Ground_vd3odhr_2K_D.EXR\n2048 x 2048");
    }
}

void BeginPanel() {
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(600.0f, 600.0f));
    ImGui::Begin("test", nullptr, ImGuiWindowFlags_NoSavedSettings);
}

// サムネイルを掴んでコンボへ落とす。
Result RunDrag(bool withTooltip) {
    Result result;
    struct Step {
        float x;
        float y;
        bool down;
    };
    // 押した位置から動かさないとドラッグにならないので、途中の移動を挟む。
    const Step steps[] = {
        {kSourcePos.x + 10.0f, kSourcePos.y + 10.0f, false},
        {kSourcePos.x + 10.0f, kSourcePos.y + 10.0f, true},
        {kSourcePos.x + 40.0f, kSourcePos.y + 60.0f, true},
        {kTargetPos.x + 40.0f, kTargetPos.y + 10.0f, true},
        {kTargetPos.x + 40.0f, kTargetPos.y + 10.0f, false},
    };

    for (const Step& step : steps) {
        Frame(step.x, step.y, step.down);
        BeginPanel();
        SubmitSource(result);
        SubmitTarget(result, withTooltip);
        ImGui::End();
        ImGui::Render();
    }
    return result;
}

// コンボの上にカーソルを置いたまま進める（表示の遅延を越えるため）。
Result RunHover(bool withTooltip) {
    Result result;
    for (int i = 0; i < 40; ++i) {
        Frame(kTargetPos.x + 40.0f, kTargetPos.y + 10.0f, false);
        BeginPanel();
        SubmitSource(result);
        SubmitTarget(result, withTooltip);
        ImGui::End();
        ImGui::Render();
        if (TooltipVisible()) {
            result.tooltipShown = true;
        }
    }
    return result;
}

}  // namespace

void RunUiInteractionTests() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    // 設定ファイルを書かせない。テストが作業ディレクトリを汚さないようにする。
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(600.0f, 600.0f);
    io.DeltaTime = 1.0f / 60.0f;
    io.Fonts->AddFontDefault();
    unsigned char* pixels = nullptr;
    int width = 0;
    int height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    io.Fonts->SetTexID(static_cast<ImTextureID>(1));

    std::printf("ImGui %s\n", IMGUI_VERSION);

    Section("ドラッグ&ドロップ");
    const Result helper = RunDrag(true);
    Check(helper.sourceStarted, "ui::ThumbnailButton はドラッグ元になる");
    Check(helper.dropped, "サムネイルからマップ欄へ割り当てられる");

    const Result withoutTooltip = RunDrag(false);
    Check(withoutTooltip.dropped, "ツールチップの有無で割り当ての成否が変わらない");

    Section("ツールチップ");
    const Result hoverOn = RunHover(true);
    Check(hoverOn.tooltipShown, "マップ欄をホバーするとツールチップが出る");
    const Result hoverOff = RunHover(false);
    Check(!hoverOff.tooltipShown, "ツールチップを積まなければ出ない（対照）");

    ImGui::DestroyContext();
}
