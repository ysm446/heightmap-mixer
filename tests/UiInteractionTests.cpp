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

// --- 列の境界（VerticalSplitter） ---------------------------------------
//
// 掴んで動かせること、範囲を越えないこと、離したときだけ true を返すことを見る。
// **どれもスクリーンショットには写らない。**

struct SplitterResult {
    float width = 0.0f;
    int releasedCount = 0;
};

// x へ向かって境界をドラッグする。steps は途中の移動を含む。
SplitterResult RunSplitterDrag(float startWidth, float dragToX, float minWidth,
                               float maxWidth) {
    SplitterResult result;
    result.width = startWidth;

    // 境界は左の列の右に余白を挟んだ位置。掴む幅の中ほどを狙う。
    // **位置は定数から導く。** 直値で書くと、余白や掴み幅を変えたときに
    // 「掴めていないのに落ちる」テストになって原因が分かりにくい。
    const float grabX = kSourcePos.x + startWidth + mm::ui::kSplitterMargin +
                        mm::ui::kSplitterGrabWidth * 0.5f;
    const float grabY = 200.0f;
    struct Step {
        float x;
        bool down;
    };
    const Step steps[] = {
        {grabX, false},   // ホバー
        {grabX, true},    // 掴む
        {dragToX, true},  // 動かす
        {dragToX, true},  // 動かし切った状態でもう 1 フレーム
        {dragToX, false},  // 離す
    };

    for (const Step& step : steps) {
        Frame(step.x, grabY, step.down);
        BeginPanel();
        ImGui::SetCursorScreenPos(kSourcePos);
        ImGui::BeginChild("left", ImVec2(result.width, 300.0f));
        ImGui::EndChild();
        if (mm::ui::VerticalSplitter("split", &result.width, minWidth, maxWidth, 300.0f)) {
            ++result.releasedCount;
        }
        ImGui::BeginChild("right", ImVec2(0.0f, 300.0f));
        ImGui::EndChild();
        ImGui::End();
        ImGui::Render();
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

    Section("列の境界（ドラッグで幅を変える）");
    // 右へ 80 動かす。掴んだ位置から素直に広がること。
    const SplitterResult widened = RunSplitterDrag(200.0f, kSourcePos.x + 280.0f, 100.0f, 400.0f);
    Check(widened.width > 260.0f && widened.width < 300.0f,
          "境界を右へドラッグすると左の列が広がる");
    Check(widened.releasedCount == 1, "離したフレームだけ true を返す（保存の合図）");

    // 上限を越えて引いても止まること。越えると隣の列が潰れる。
    const SplitterResult clamped = RunSplitterDrag(200.0f, kSourcePos.x + 900.0f, 100.0f, 260.0f);
    Check(clamped.width <= 260.0f, "上限を越えて広がらない");

    // 下限も同じ。左へ引き切っても一覧が消えない。
    const SplitterResult shrunk = RunSplitterDrag(200.0f, kSourcePos.x - 400.0f, 150.0f, 400.0f);
    Check(shrunk.width >= 150.0f, "下限を割って縮まない");

    ImGui::DestroyContext();
}
