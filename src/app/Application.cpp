#include "app/Application.h"

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
namespace {

// クライアント領域（描画される中身）のサイズ。ウィンドウ枠は含まない。
// DPI では拡大しない。スクリーンショットや録画の解像度を固定するため。
constexpr uint32_t kInitialWidth = 1920;
constexpr uint32_t kInitialHeight = 1080;

// ホットリロードの走査間隔（フレーム数）。毎フレーム走査するほどの頻度は要らない。
constexpr uint32_t kHotReloadIntervalFrames = 30;

#if defined(MM_DEBUG)
constexpr bool kEnableDebugLayer = true;
#else
constexpr bool kEnableDebugLayer = false;
#endif

// シェーダの探索先。環境変数 MM_SHADER_DIR で差し替えられるようにしておく。
std::filesystem::path ResolveShaderRoot() {
    const DWORD needed = ::GetEnvironmentVariableW(L"MM_SHADER_DIR", nullptr, 0);
    if (needed > 0) {
        std::wstring value;
        value.resize(needed);
        const DWORD written = ::GetEnvironmentVariableW(L"MM_SHADER_DIR", value.data(), needed);
        if (written > 0) {
            value.resize(written);
            return std::filesystem::path(value);
        }
    }
    return std::filesystem::path(MM_SHADER_DIR);
}

// ImGui へ渡す文字列は UTF-8。path::string() はロケール依存なので使わない。
std::string ToUtf8(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}

std::filesystem::path FromUtf8(const std::string& text) {
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

float RadiansToDegrees(float radians) {
    return radians * (180.0f / 3.14159265358979323846f);
}

float DegreesToRadians(float degrees) {
    return degrees * (3.14159265358979323846f / 180.0f);
}

// -pi .. pi へ折り返す。方位角を一周させるときに使う（UI のスライダーもこの範囲）。
float WrapAngle(float radians) {
    constexpr float kPi = 3.14159265358979323846f;
    constexpr float kTwoPi = kPi * 2.0f;
    radians = std::fmod(radians + kPi, kTwoPi);
    if (radians < 0.0f) {
        radians += kTwoPi;
    }
    return radians - kPi;
}

}  // namespace

namespace {

// 既定値マーカーが参照する値。数値リテラルではなく設定構造体の初期値を使う。
const compositor::MaterialLayer kDefaultLayer;
const compositor::BrushSettings kDefaultBrush;
const renderer::LightSettings kDefaultLight;
const renderer::ExposureSettings kDefaultExposure;
const renderer::MaterialSettings kDefaultMaterial;
const renderer::CameraState kDefaultCamera;
const renderer::SkySettings kDefaultSky;

const char* const kNoiseTypeLabels[] = {"fBm", "尾根状", "セル状"};
const char* const kValueSourceLabels[] = {"定数", "ノイズ", "テクスチャ"};
const char* const kMaskSourceLabels[] = {
    "定数",       "ノイズ",     "テクスチャ", "下地の高さ",
    "下地の傾斜", "下地の曲率", "下地の窪み", "ペイント",
};
const char* const kChannelLabels[] = {"BaseColor", "Normal", "Surface", "Height"};

// ビューポートの表示モード。renderer::DebugView と並びを合わせること。
const char* const kDebugViewLabels[] = {
    "シェーディング", "ベースカラー", "法線（接空間）", "法線（ワールド）",
    "ラフネス",       "メタルネス",   "AO",             "ハイト",
    "ワイヤーフレーム",
};
const char* const kResolutionLabels[] = {"512", "1024", "2048"};
constexpr uint32_t kResolutionValues[] = {512, 1024, 2048};

// レイヤー一覧のドラッグ＆ドロップで使うペイロードの種別。
constexpr const char* kLayerDragDropType = "MM_LAYER";
// テクスチャ一覧からマップ欄へのドラッグ＆ドロップで使うペイロードの種別。
constexpr const char* kTextureDragDropType = "MM_TEXTURE";
constexpr const char* kTextureRemoveModalTitle = "テクスチャを削除";

// テクスチャの一覧に出すフォーマット名。DXGI の名前は長いので短く言い換える。
const char* TextureFormatLabel(const compositor::LibraryTexture& entry) {
    return entry.isFloat ? "RGBA16F (リニア)" : "RGBA8 (sRGB / リニア)";
}

// ステータスバーの通知を残す時間（秒）。情報だけが時間で消える。
constexpr float kStatusHoldSeconds = 6.0f;

// ビューポートの背景色の既定値。Application のメンバ初期化と揃えること。
constexpr float kDefaultClearColor[3] = {0.09f, 0.09f, 0.11f};

// 解像度コンボの選択位置。一致するものが無ければ 1（1024）に寄せる。
int ResolutionIndex(uint32_t resolution) {
    for (int i = 0; i < IM_ARRAYSIZE(kResolutionValues); ++i) {
        if (kResolutionValues[i] == resolution) {
            return i;
        }
    }
    return 1;
}

// ノイズの種類を選ぶ行。
bool DrawNoiseTypeRow(const char* label, compositor::NoiseType& type,
                      compositor::NoiseType defaultType) {
    int selected = static_cast<int>(type);
    if (ui::PropertyCombo(label, &selected, kNoiseTypeLabels, IM_ARRAYSIZE(kNoiseTypeLabels),
                          static_cast<int>(defaultType),
                          "fBm: 一般的な起伏 / 尾根状: 稜線や割れ目 / セル状: 石畳や砂利")) {
        type = static_cast<compositor::NoiseType>(selected);
        return true;
    }
    return false;
}

// ノイズのパラメータをまとめて並べる。ハイトとマスクで共通。
// ハイトでは寄与の量を heightGain が担うので、showAmount を false にして「量」を出さない。
bool DrawNoiseRows(compositor::NoiseParams& noise, const compositor::NoiseParams& defaults,
                   bool showAmount = true) {
    bool changed = DrawNoiseTypeRow("種類", noise.type, defaults.type);
    changed |= ui::PropertyFloat("周波数", &noise.scale, 0.5f, 64.0f, defaults.scale,
                                 "大きいほど細かい模様になる", "%.1f", 0, 0.5f);
    if (showAmount) {
        changed |= ui::PropertyFloat("量", &noise.amount, 0.0f, 3.0f, defaults.amount,
                                     "ノイズの寄与。0 で効かなくなる", "%.2f");
    }
    changed |= ui::PropertyInt("オクターブ", &noise.octaves, 1, 8, defaults.octaves,
                               "重ねる段数。多いほど細部が増え、計算も増える");
    changed |= ui::PropertyFloat("オフセット", &noise.offset, 0.0f, 64.0f, defaults.offset,
                                 "同じ設定で別の模様がほしいときにずらす", "%.1f", 0, 0.5f);
    return changed;
}

// マテリアルを選ぶ行。サムネイル付きの一覧から選ぶ。
bool DrawMaterialSlotRow(const char* label, compositor::MaterialAssetId& slot,
                         const compositor::MaterialLibrary& library) {
    ui::PropertyLabel(label, "「なし」ならレイヤーの定数値だけで塗る");

    std::string preview = "なし";
    if (const compositor::MaterialAsset* current = library.Find(slot); current != nullptr) {
        preview = current->name;
    }

    const float thumbnailSize = ImGui::GetFrameHeight();
    bool changed = false;
    ImGui::SetNextItemWidth(
        std::min(ui::Scaled(ui::kComboMaxWidth), ImGui::GetContentRegionAvail().x));
    if (ImGui::BeginCombo("##value", preview.c_str())) {
        if (ImGui::Selectable("なし", slot == compositor::kNoMaterialAsset)) {
            slot = compositor::kNoMaterialAsset;
            changed = true;
        }
        for (const compositor::MaterialAsset& asset : library.Entries()) {
            ImGui::PushID(static_cast<int>(asset.id));
            if (asset.thumbnail.IsValid()) {
                ImGui::Image(static_cast<ImTextureID>(asset.thumbnail.srv.gpu.ptr),
                             ImVec2(thumbnailSize, thumbnailSize));
                ImGui::SameLine();
            }
            if (ImGui::Selectable(asset.name.c_str(), slot == asset.id)) {
                slot = asset.id;
                changed = true;
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    ui::PropertyEnd();
    return changed;
}

// テクスチャを選ぶコンボ。行の中に置く部品。
bool DrawTextureCombo(const char* id, compositor::TextureId& slot,
                      const compositor::TextureLibrary& library, float width) {
    std::string preview = "なし";
    if (const compositor::LibraryTexture* current = library.Find(slot); current != nullptr) {
        preview = current->name;
    }

    bool changed = false;
    ImGui::SetNextItemWidth(width);
    if (ImGui::BeginCombo(id, preview.c_str())) {
        if (ImGui::Selectable("なし", slot == compositor::kNoTexture)) {
            slot = compositor::kNoTexture;
            changed = true;
        }
        for (const compositor::LibraryTexture& entry : library.Entries()) {
            ImGui::PushID(static_cast<int>(entry.id));
            if (ImGui::Selectable(entry.name.c_str(), slot == entry.id)) {
                slot = entry.id;
                changed = true;
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }

    // テクスチャ一覧からドラッグしてきた画像を受ける。
    // ドラッグ中にコンボは開けないので、直前のアイテムは必ずコンボ本体になる。
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kTextureDragDropType);
            payload != nullptr) {
            slot = *static_cast<const compositor::TextureId*>(payload->Data);
            changed = true;
        }
        ImGui::EndDragDropTarget();
    }
    return changed;
}

// テクスチャスロットを選ぶ行。RGB をそのまま使うマップ（ベースカラー / 法線）用。
bool DrawTextureSlotRow(const char* label, compositor::TextureId& slot,
                        const compositor::TextureLibrary& library) {
    ui::PropertyLabel(label, "「なし」なら定数値を使う");
    const float width =
        std::min(ui::Scaled(ui::kComboMaxWidth), ImGui::GetContentRegionAvail().x);
    const bool changed = DrawTextureCombo("##value", slot, library, width);
    ui::PropertyEnd();
    return changed;
}

// スカラーのマップを選ぶ行。テクスチャに加えて、どのチャンネルを読むかも選ぶ。
// Megascans の _ORD のように 1 枚へ複数のマップを詰めたテクスチャがあるため。
bool DrawMapSlotRow(const char* label, compositor::MapSlot& slot,
                    const compositor::TextureLibrary& library) {
    ui::PropertyLabel(label, "「なし」なら定数値を使う。右は読むチャンネル");

    const float channelWidth = ui::Scaled(52.0f);
    const float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
    const float available = ImGui::GetContentRegionAvail().x;
    const float comboWidth =
        std::max(ui::Scaled(60.0f),
                 std::min(ui::Scaled(ui::kComboMaxWidth), available) - channelWidth - spacing);

    bool changed = DrawTextureCombo("##texture", slot.texture, library, comboWidth);

    if (slot.texture != compositor::kNoTexture) {
        ImGui::SameLine(0.0f, spacing);
        static const char* const kTextureChannelLabels[] = {"R", "G", "B", "A"};
        int channel = static_cast<int>(slot.channel);
        ImGui::SetNextItemWidth(channelWidth);
        if (ImGui::Combo("##channel", &channel, kTextureChannelLabels,
                         IM_ARRAYSIZE(kTextureChannelLabels))) {
            slot.channel = static_cast<compositor::TextureChannel>(channel);
            changed = true;
        }
    }

    ui::PropertyEnd();
    return changed;
}

// ライトのギズモが残る時間（秒）。掴むのをやめてから薄くなって消える。
constexpr double kLightGizmoFadeSeconds = 0.35;
// ライトを掴んだときの感度。参考にした terrain-editor と同じ 0.25 度 / ピクセル。
constexpr float kLightDegreesPerPixel = 0.25f;

// ビューポートに重ねる線を描くための投影。カメラの行列をそのまま使う。
struct ProjectedPoint {
    ImVec2 screen{};
    bool visible = false;
};

ProjectedPoint ProjectToViewport(const DirectX::XMMATRIX& viewProjection,
                                 const DirectX::XMFLOAT3& world, const ImVec2& min,
                                 const ImVec2& size) {
    using namespace DirectX;
    const XMVECTOR clip = XMVector3Transform(XMLoadFloat3(&world), viewProjection);
    const float w = XMVectorGetW(clip);
    ProjectedPoint out;
    // カメラの後ろに回った点は描かない。
    if (w <= 1e-4f) {
        return out;
    }
    const float ndcX = XMVectorGetX(clip) / w;
    const float ndcY = XMVectorGetY(clip) / w;
    out.screen = ImVec2(min.x + (ndcX * 0.5f + 0.5f) * size.x,
                        min.y + (0.5f - ndcY * 0.5f) * size.y);
    out.visible = true;
    return out;
}

// ビューポート左下に置く座標軸ギズモ。
//
// 軸の色は DCC 共通の意味色（X=赤 / Y=緑 / Z=青）なので、テーマからは引かない。
// 透視投影は掛けず、向きだけを見せる。
void DrawAxisGizmo(const renderer::Camera& camera, const ImVec2& viewportMin,
                   const ImVec2& viewportMax) {
    const renderer::CameraBasis basis = camera.Basis();

    const float radius = ui::Scaled(30.0f);
    const float margin = ui::Scaled(16.0f);
    const ImVec2 center(viewportMin.x + margin + radius, viewportMax.y - margin - radius);
    if (center.x + radius > viewportMax.x || center.y - radius < viewportMin.y) {
        return;  // ビューポートが小さすぎる。
    }

    struct Axis {
        float direction[3];
        ImU32 color;
        const char* label;
    };
    static const Axis kAxes[] = {
        {{1.0f, 0.0f, 0.0f}, IM_COL32(226, 96, 96, 255), "X"},
        {{0.0f, 1.0f, 0.0f}, IM_COL32(124, 196, 104, 255), "Y"},
        {{0.0f, 0.0f, 1.0f}, IM_COL32(96, 146, 226, 255), "Z"},
    };

    struct Projected {
        ImVec2 tip;
        float depth = 0.0f;  // 正なら画面の奥を向いている
        ImU32 color = 0;
        const char* label = nullptr;
    };

    const auto project = [](const float* axis, const DirectX::XMFLOAT3& b) {
        return axis[0] * b.x + axis[1] * b.y + axis[2] * b.z;
    };

    Projected projected[IM_ARRAYSIZE(kAxes)];
    for (int i = 0; i < IM_ARRAYSIZE(kAxes); ++i) {
        const float x = project(kAxes[i].direction, basis.right);
        const float y = project(kAxes[i].direction, basis.up);
        projected[i].depth = project(kAxes[i].direction, basis.forward);
        projected[i].tip = ImVec2(center.x + x * radius, center.y - y * radius);
        projected[i].label = kAxes[i].label;

        // 奥を向いている軸は落として、手前と見分けられるようにする。
        const ImU32 color = kAxes[i].color;
        projected[i].color = (projected[i].depth > 0.0f)
                                 ? ((color & ~IM_COL32_A_MASK) | (110u << IM_COL32_A_SHIFT))
                                 : color;
    }

    // 奥の軸から先に描く。
    std::sort(std::begin(projected), std::end(projected),
              [](const Projected& a, const Projected& b) { return a.depth > b.depth; });

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const float dotRadius = ui::Scaled(7.0f);
    for (const Projected& axis : projected) {
        drawList->AddLine(center, axis.tip, axis.color, ui::Scaled(1.6f));
        drawList->AddCircleFilled(axis.tip, dotRadius, axis.color);

        const ImVec2 textSize = ImGui::CalcTextSize(axis.label);
        const ImVec2 textPos(axis.tip.x - textSize.x * 0.5f, axis.tip.y - textSize.y * 0.5f);
        drawList->AddText(textPos, IM_COL32(22, 22, 22, 235), axis.label);
    }
}

}  // namespace

bool Application::Initialize(const StartupOptions& options) {
    m_options = options;

    // ファイル選択ダイアログ（IFileDialog）が COM を使う。
    if (FAILED(::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE))) {
        MM_LOG_WARN("COM を初期化できませんでした。ファイル選択ダイアログは使えません");
    }

    // ウィンドウ生成より前に済ませる必要がある。
    ImGuiLayer::EnableDpiAwareness();

    // クライアント領域を実ピクセルで 1920x1080 にする。DPI では拡大しない。
    // UI の大きさは ImGui 側の DPI スケールで合わせる。
    // モニタからはみ出す場合は Window::Create 側で作業領域に収める。
    if (!m_window.Create(L"Material Mixer", kInitialWidth, kInitialHeight)) {
        return false;
    }

    if (!m_device.Initialize(m_window.Handle(), m_window.Width(), m_window.Height(),
                             kEnableDebugLayer)) {
        return false;
    }

    if (!m_shaderCompiler.Create(ResolveShaderRoot())) {
        return false;
    }
    if (!m_pipelineCache.Create(m_device.GetDevice(), &m_shaderCompiler)) {
        return false;
    }
    if (!m_renderer.Initialize(m_device, m_pipelineCache)) {
        return false;
    }
    if (!m_renderer.Resize(m_device, m_requestedViewportWidth, m_requestedViewportHeight)) {
        return false;
    }

    if (!m_imgui.Initialize(m_window, m_device)) {
        return false;
    }

    m_window.SetMessageHook([this](HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
        return m_imgui.HandleMessage(hwnd, msg, wparam, lparam);
    });
    m_window.SetResizeCallback([this](uint32_t width, uint32_t height) {
        m_device.Resize(width, height);
    });
    // エクスプローラからのドロップ。拡張子で行き先を振り分ける。
    m_window.SetDropCallback([this](const std::vector<std::filesystem::path>& paths) {
        HandleDroppedFiles(paths);
    });

    m_pendingTexturePaths = options.texturePaths;

    if (!options.hdriPath.empty()) {
        m_renderer.RequestHdrLoad(options.hdriPath);
    }
    // 読み込みは GPU 待機を伴うので、ここでは要求だけ積む。
    // 最初のフレームの前に ProcessPendingFileWork が処理する。
    if (!options.projectPath.empty()) {
        m_pendingProjectOpen = options.projectPath;
    }
    UpdateWindowTitle();

    m_settings.Load();
    m_recentProjects.Load();
    // 設定に拡大率が残っていれば、ウィンドウの大きさもそれに合わせる。
    ApplyUiScale();

    // ログをステータスバーへ流す。以降の警告やエラーは画面上でも見える。
    SetLogSink([this](LogLevel level, const char* text) { PushStatus(level, text); });

    MM_LOG_INFO("material-mixer %s を起動しました", MM_APP_VERSION);
    return true;
}

void Application::Shutdown() {
    // シンクは this を掴んでいる。破棄より先に必ず外す。
    SetLogSink({});

    m_device.WaitForGpu();
    m_paintMasks.Destroy(m_device);
    m_materialLibrary.Destroy(m_device);
    m_textureLibrary.Destroy(m_device);
    m_renderer.Shutdown(m_device);
    m_imgui.Shutdown();
    m_pipelineCache.Destroy();
    m_shaderCompiler.Destroy();
    m_device.Shutdown();
    m_window.Destroy();
    ::CoUninitialize();
}

void Application::PollShaderHotReload() {
    if (!m_hotReloadEnabled) {
        return;
    }
    if ((m_frameCounter % kHotReloadIntervalFrames) != 0) {
        return;
    }
    if (!m_shaderCompiler.PollChanges()) {
        return;
    }

    MM_LOG_INFO("シェーダの更新を検出しました。PSO を作り直します");
    // PSO は GPU が参照中の可能性があるため、破棄前に必ず待つ。
    m_device.WaitForGpu();
    m_pipelineCache.InvalidateAll();
}

int Application::Run() {
    while (m_window.PumpMessages()) {
        if (m_window.IsMinimized()) {
            ::WaitMessage();
            continue;
        }

        PollShaderHotReload();



        // UI の拡大率はスタイル・フォントとウィンドウの大きさに効く。フレームの外で。
        ApplyUiScale();

        // プロジェクトとマテリアルの読み書きも GPU 待機を伴うため、フレームの外で。
        // 他の保留処理より先に行う（読み込みが中身を丸ごと入れ替えるため）。
        ProcessPendingFileWork();

        // 開発用: 数フレーム描いてからプロジェクトを保存して終了する。
        // 対話せずに保存と読み込みを確かめるために使う。
        if (!m_options.saveProjectPath.empty() && m_frameCounter >= m_options.screenshotFrame) {
            const io::ProjectRefs refs{m_materialStack, m_textureLibrary, m_materialLibrary,
                                       m_paintMasks, m_renderer};
            io::SaveProject(m_options.saveProjectPath, m_device, refs);
            break;
        }

        // 環境マップやマテリアル解像度の作り直しは GPU 待機を伴うため、
        // フレームの外で処理する。
        m_renderer.ProcessPendingWork(m_device, m_pipelineCache);
        // ペイントマスクの解像度変更も作り直しを伴うため、フレームの外で処理する。
        m_paintMasks.ProcessPendingWork(m_device, m_pipelineCache);

        // テクスチャ読み込みも GPU 待機を伴うため、フレームの外で処理する。
        if (!m_pendingTexturePaths.empty()) {
            std::vector<std::filesystem::path> paths;
            paths.swap(m_pendingTexturePaths);
            bool loaded = false;
            for (const std::filesystem::path& path : paths) {
                const compositor::TextureId id =
                    m_textureLibrary.Load(m_device, m_pipelineCache, path);
                if (id == compositor::kNoTexture) {
                    continue;
                }
                loaded = true;
                // 読み込んだものを選択して一覧に見せる。
                m_selectedTexture =
                    static_cast<int>(m_textureLibrary.Entries().size()) - 1;
                m_scrollToSelectedTexture = true;
            }
            if (loaded) {
                // 読み込んだ画像を参照しているサムネイルを作り直す。
                for (const compositor::MaterialAsset& asset : m_materialLibrary.Entries()) {
                    m_materialLibrary.MarkThumbnailDirty(asset.id);
                }
                m_materialStack.MarkDirty();
            }
        }

        // サムネイルの生成も GPU 待機を伴う。
        m_materialLibrary.ProcessPendingWork(m_device, m_pipelineCache, m_textureLibrary);

        // ビューポートの作り直しは GPU 待機を伴うため、フレームの外で行う。
        if (m_requestedViewportWidth != m_renderer.Width() ||
            m_requestedViewportHeight != m_renderer.Height()) {
            m_renderer.Resize(m_device, m_requestedViewportWidth, m_requestedViewportHeight);
        }

        m_imgui.BeginFrame();
        DrawUi();

        ID3D12GraphicsCommandList* commandList = m_device.BeginFrame(m_clearColor);
        if (commandList == nullptr) {
            // フレームを開始できなかった場合は ImGui の状態を捨てて次へ進む。
            ImGui::EndFrame();
            continue;
        }

        // ブラシは前フレームの UV バッファを読むため、合成の評価より前に流す。
        const compositor::PaintContext paintContext =
            m_renderer.PrepareUvBufferForRead(commandList);
        if (m_paintMasks.Process(m_device, m_pipelineCache, commandList, paintContext)) {
            // マスクの中身が変わったので合成をやり直す。
            m_materialStack.MarkDirty();
        }

        m_renderer.Render(m_device, m_pipelineCache, commandList, m_materialStack,
                          m_textureLibrary, m_materialLibrary, m_paintMasks);

        // レンダラがターゲットを差し替えているので、ImGui を描く前に戻す。
        m_device.BindBackBuffer(commandList);
        m_imgui.EndFrame(commandList);

        // UI 込みの書き出しは、バックバッファが描き終わったこのフレームで写す。
        const bool captureUi = !m_options.uiScreenshotPath.empty() &&
                               (m_frameCounter + 1) >= m_options.screenshotFrame;
        if (captureUi) {
            m_device.RequestBackBufferCapture(m_options.uiScreenshotPath);
        }

        m_device.EndFrame(m_vsync);
        ++m_frameCounter;

        // 開発用のスクリーンショット。書き出したら終了する。
        if (captureUi) {
            break;
        }
        if (!m_options.screenshotPath.empty() && m_frameCounter >= m_options.screenshotFrame) {
            m_device.WaitForGpu();
            m_renderer.SaveOutputToPng(m_device, m_options.screenshotPath);
            break;
        }
    }
    return 0;
}

void Application::DrawUi() {
    // ショートカットはメニューを開いていなくても効かせたいので、先に見る。
    HandleShortcuts();

    // メニューバーを先に作ることで、メインビューポートの作業領域が
    // メニューバー分を差し引いた状態になる。既定のパネル配置がこれに依存する。
    if (ImGui::BeginMainMenuBar()) {
        DrawFileMenu();
        if (ImGui::BeginMenu("表示")) {
            if (ImGui::MenuItem("レイアウトをリセット")) {
                m_rebuildLayout = true;
            }
            ImGui::Separator();
            ImGui::MenuItem("設定", nullptr, &m_showSettings);
            ImGui::MenuItem("ImGui デモ", nullptr, &m_showDemoWindow);
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    // パネルはすべてドックへ収める。絶対座標で置くと、ウィンドウの大きさが
    // 変わったときや、別の大きさで保存された ini を読んだときに画面外へはみ出す。
    // ドックスペースの ID には版を付ける。**パネルを増減したら版を上げること。**
    // ID が変われば ini に配置が無い状態になり、既定レイアウトが組み直される。
    // 上げないと、新しいパネルがどこにも入らず浮いたままになる。
    const ImGuiID dockspaceId = ImGui::GetID("MaterialMixerDockSpace_v5");

    // ステータスバーもメニューバーと同じく、先に作って作業領域を狭めておく。
    DrawStatusBar();

    // ini にドックの配置が無ければ既定レイアウトを組む。
    // DockSpaceOverViewport がノードを作る前に判定すること。
    if (!m_layoutChecked) {
        m_layoutChecked = true;
        m_rebuildLayout = (ImGui::DockBuilderGetNode(dockspaceId) == nullptr);
    }
    if (m_rebuildLayout) {
        m_rebuildLayout = false;
        BuildDefaultLayout(dockspaceId);
    }

    ImGui::DockSpaceOverViewport(dockspaceId, ImGui::GetMainViewport());

    DrawViewportPanel();
    // タブが重なる枠では、**最初に submit したパネルが前面のタブになり、
    // タブは submit した順に並ぶ**（ini に配置が無いとき）。
    // 作業の起点はレイヤーなので、同じ枠のマテリアル・テクスチャより先に描く。
    DrawLayerPanel();
    DrawMaterialLibraryPanel();
    DrawTextureLibraryPanel();
    DrawMaterialPanel();
    DrawLightingPanel();
    DrawInfoPanel();

    DrawSettingsWindow();

    if (m_showDemoWindow) {
        ImGui::ShowDemoWindow(&m_showDemoWindow);
    }

    if (m_focusDefaultTabs > 0) {
        --m_focusDefaultTabs;
    }
}

compositor::MaterialLayer* Application::CurrentPaintLayer() {
    if (!m_paintMode) {
        return nullptr;
    }
    std::vector<compositor::MaterialLayer>& layers = m_materialStack.Layers();
    if (layers.empty() || m_selectedLayer < 0 ||
        m_selectedLayer >= static_cast<int>(layers.size())) {
        return nullptr;
    }

    compositor::MaterialLayer& layer = layers[static_cast<size_t>(m_selectedLayer)];
    if (layer.mask.source != compositor::MaskSource::Paint ||
        layer.mask.paint == compositor::kNoPaintMask) {
        return nullptr;
    }
    return &layer;
}

// ビューポートに重ねる操作。いまは表示モードの切り替えだけ。
//
// トップメニューではなくビューポートの中に置く。見ている場所から目を離さずに
// 切り替えられ、いまどの表示なのかも常に見える。
void Application::DrawViewportOverlay(const ImVec2& viewportMin) {
    const float margin = ui::Scaled(10.0f);
    ImGui::SetCursorScreenPos(ImVec2(viewportMin.x + margin, viewportMin.y + margin));

    renderer::DebugView& current = m_renderer.Debug();
    const char* label = kDebugViewLabels[static_cast<size_t>(current)];

    // 既定以外の表示は見落としやすいので、ボタンの文字を強調する。
    const bool highlighted = (current != renderer::DebugView::Shaded);
    if (highlighted) {
        ImGui::PushStyleColor(ImGuiCol_Text, ui::WarnColor());
    }
    char buttonLabel[96] = {};
    std::snprintf(buttonLabel, sizeof(buttonLabel), "%s  v", label);
    if (ImGui::Button(buttonLabel)) {
        ImGui::OpenPopup("##viewportViewMenu");
    }
    if (highlighted) {
        ImGui::PopStyleColor();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("ビューポートに何を表示するか");
    }

    if (ImGui::BeginPopup("##viewportViewMenu")) {
        for (int i = 0; i < IM_ARRAYSIZE(kDebugViewLabels); ++i) {
            const auto view = static_cast<renderer::DebugView>(i);
            if (ImGui::Selectable(kDebugViewLabels[i], current == view)) {
                current = view;
            }
        }
        ImGui::EndPopup();
    }
}

// L + 左ドラッグでライトの向きを変える。
//
// 修飾キー（Ctrl / Shift / Alt）は付けない。Alt は軌道、Ctrl は数値の直接入力に
// 使っているので、それらと重ならないようにする。
bool Application::HandleLightDrag(bool itemActive) {
    const ImGuiIO& io = ImGui::GetIO();
    const bool shortcut =
        ImGui::IsKeyDown(ImGuiKey_L) && !io.KeyCtrl && !io.KeyShift && !io.KeyAlt;
    if (!shortcut || !itemActive || !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        m_lightDragActive = false;
        return false;
    }

    m_lightDragActive = true;
    m_lightGizmoUntil = ImGui::GetTime() + kLightGizmoFadeSeconds;

    renderer::LightSettings& light = m_renderer.Light();
    const float step = DegreesToRadians(kLightDegreesPerPixel);
    // 方位角は一周させる。仰角は UI のスライダーと同じ範囲に収める。
    light.azimuth = WrapAngle(light.azimuth + io.MouseDelta.x * step);
    // 真下からの光も見たいので、下は -89 度まで許す。
    light.elevation = std::clamp(light.elevation - io.MouseDelta.y * step,
                                 DegreesToRadians(-89.0f), DegreesToRadians(89.0f));
    return true;
}

// F でメッシュを画面の中心へ戻し、A でさらに全体が収まる距離まで引く。
// DCC の「選択をフレーム / 全体をフレーム」に倣った割り当て。
//
// 修飾キーは付けない（Ctrl は数値の直接入力、Alt は軌道に使っている）。
// カーソルがビューポートの上にあるときだけ効かせ、
// **テキスト入力中は無視する**。レイヤー名を打っている最中に視点が飛ぶのを防ぐ。
void Application::HandleCameraShortcuts(bool itemHovered) {
    const ImGuiIO& io = ImGui::GetIO();
    if (!itemHovered || io.WantTextInput || io.KeyCtrl || io.KeyShift || io.KeyAlt) {
        return;
    }

    // プレビューのメッシュはどれも原点中心（モデル行列は単位行列）。
    constexpr DirectX::XMFLOAT3 kMeshCenter{0.0f, 0.0f, 0.0f};
    renderer::Camera& camera = m_renderer.GetCamera();

    if (ImGui::IsKeyPressed(ImGuiKey_F, false)) {
        camera.Focus(kMeshCenter);
    } else if (ImGui::IsKeyPressed(ImGuiKey_A, false)) {
        camera.Frame(kMeshCenter, m_renderer.BoundingRadius());
    }
}

// ライトの向きを示すギズモ。地面のリング、水平方向、仰角の弧、光が来る向きの矢印。
//
// 色はテーマから引かない。座標軸ギズモと同じく「意味を持つ色」として固定する。
void Application::DrawLightGizmo(const ImVec2& viewportMin, const ImVec2& viewportMax) {
    const double now = ImGui::GetTime();
    if (!m_lightDragActive && now >= m_lightGizmoUntil) {
        return;
    }
    const float fade =
        m_lightDragActive
            ? 1.0f
            : static_cast<float>(std::clamp((m_lightGizmoUntil - now) / kLightGizmoFadeSeconds,
                                            0.0, 1.0));
    if (fade <= 0.001f) {
        return;
    }

    using namespace DirectX;
    const renderer::Camera& camera = m_renderer.GetCamera();
    const XMMATRIX viewProjection = camera.ViewMatrix() * camera.ProjectionMatrix();
    const ImVec2 size(viewportMax.x - viewportMin.x, viewportMax.y - viewportMin.y);
    if (size.x <= 0.0f || size.y <= 0.0f) {
        return;
    }

    const renderer::LightSettings& light = m_renderer.Light();
    const XMFLOAT3 direction = light.Direction();
    // プレビューのメッシュ（半径 1）の外側を通す。
    constexpr float kRadius = 1.5f;
    const XMFLOAT3 origin{0.0f, 0.0f, 0.0f};
    const XMFLOAT3 horizontal{std::sin(light.azimuth), 0.0f, std::cos(light.azimuth)};

    const auto color = [fade](int r, int g, int b, int a) {
        return IM_COL32(r, g, b, static_cast<int>(static_cast<float>(a) * fade));
    };
    const auto offset = [](const XMFLOAT3& base, const XMFLOAT3& dir, float amount) {
        return XMFLOAT3{base.x + dir.x * amount, base.y + dir.y * amount,
                        base.z + dir.z * amount};
    };
    const auto project = [&](const XMFLOAT3& world) {
        return ProjectToViewport(viewProjection, world, viewportMin, size);
    };

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->PushClipRect(viewportMin, viewportMax, true);

    const auto drawWorldLine = [&](const XMFLOAT3& a, const XMFLOAT3& b, ImU32 lineColor,
                                   float thickness) {
        const ProjectedPoint pa = project(a);
        const ProjectedPoint pb = project(b);
        if (pa.visible && pb.visible) {
            drawList->AddLine(pa.screen, pb.screen, lineColor, thickness);
        }
    };

    // 地面のリング。方位角の目安になる。
    constexpr int kRingSegments = 72;
    ProjectedPoint previous;
    for (int i = 0; i <= kRingSegments; ++i) {
        const float t = (static_cast<float>(i) / kRingSegments) * 2.0f * 3.14159265f;
        const ProjectedPoint current =
            project(XMFLOAT3{std::sin(t) * kRadius, 0.0f, std::cos(t) * kRadius});
        if (i > 0 && previous.visible && current.visible) {
            drawList->AddLine(previous.screen, current.screen, color(150, 160, 175, 130), 1.6f);
        }
        previous = current;
    }

    // 水平方向への投影と、そこから仰角ぶんの弧。
    drawWorldLine(origin, offset(origin, horizontal, kRadius), color(150, 160, 175, 170), 1.8f);

    constexpr int kArcSegments = 32;
    ProjectedPoint previousArc;
    for (int i = 0; i <= kArcSegments; ++i) {
        const float angle = light.elevation * (static_cast<float>(i) / kArcSegments);
        const ProjectedPoint current = project(XMFLOAT3{horizontal.x * std::cos(angle) * kRadius,
                                                        std::sin(angle) * kRadius,
                                                        horizontal.z * std::cos(angle) * kRadius});
        if (i > 0 && previousArc.visible && current.visible) {
            drawList->AddLine(previousArc.screen, current.screen, color(255, 206, 112, 150), 1.6f);
        }
        previousArc = current;
    }

    // 光が来る向きの矢印。ライトの位置から原点へ向ける。
    const ProjectedPoint arrowStart = project(offset(origin, direction, kRadius));
    const ProjectedPoint arrowEnd = project(offset(origin, direction, kRadius * 0.22f));
    if (arrowStart.visible && arrowEnd.visible) {
        const ImU32 lightColor = color(255, 188, 76, 245);
        ImVec2 screenDir(arrowEnd.screen.x - arrowStart.screen.x,
                         arrowEnd.screen.y - arrowStart.screen.y);
        const float length = std::sqrt(screenDir.x * screenDir.x + screenDir.y * screenDir.y);
        if (length > 0.001f) {
            screenDir.x /= length;
            screenDir.y /= length;
            const ImVec2 side(-screenDir.y, screenDir.x);
            const float head = ui::Scaled(14.0f);
            const float halfWidth = ui::Scaled(6.0f);
            const ImVec2 base(arrowEnd.screen.x - screenDir.x * head,
                              arrowEnd.screen.y - screenDir.y * head);
            drawList->AddLine(arrowStart.screen, base, lightColor, ui::Scaled(3.5f));
            drawList->AddTriangleFilled(
                arrowEnd.screen, ImVec2(base.x + side.x * halfWidth, base.y + side.y * halfWidth),
                ImVec2(base.x - side.x * halfWidth, base.y - side.y * halfWidth), lightColor);
        }
    }

    if (const ProjectedPoint center = project(origin); center.visible) {
        drawList->AddCircle(center.screen, ui::Scaled(5.0f), color(200, 210, 220, 200), 20, 1.6f);
    }

    // いまの値。掴んだまま数字を確かめられるようにする。
    char text[64] = {};
    std::snprintf(text, sizeof(text), "方位角 %.0f 度   仰角 %.0f 度",
                  RadiansToDegrees(light.azimuth), RadiansToDegrees(light.elevation));
    const ImVec2 textSize = ImGui::CalcTextSize(text);
    const ImVec2 padding(ui::Scaled(8.0f), ui::Scaled(5.0f));
    // 左上には表示モードのボタンがあるので、その下へ置く。
    const ImVec2 textMin(viewportMin.x + ui::Scaled(10.0f),
                         viewportMin.y + ui::Scaled(10.0f) + ImGui::GetFrameHeight() +
                             ui::Scaled(6.0f));
    const ImVec2 textMax(textMin.x + textSize.x + padding.x * 2.0f,
                         textMin.y + textSize.y + padding.y * 2.0f);
    drawList->AddRectFilled(textMin, textMax, color(8, 10, 12, 190), ui::Scaled(4.0f));
    drawList->AddText(ImVec2(textMin.x + padding.x, textMin.y + padding.y),
                      color(235, 235, 235, 255), text);

    drawList->PopClipRect();
}

void Application::HandlePaintInput(compositor::MaterialLayer& layer, bool itemActive,
                                   const ImVec2& imageOrigin, const ImVec2& imageSize) {
    const ImGuiIO& io = ImGui::GetIO();

    const bool addPressed = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    const bool erasePressed = ImGui::IsMouseDown(ImGuiMouseButton_Right);
    if (!itemActive || (!addPressed && !erasePressed)) {
        m_strokeActive = false;
        return;
    }

    // 画像はコンテンツ領域に合わせて拡縮して描いているため、
    // ImGui の座標をレンダーターゲットのピクセル座標へ換算する。
    const float scaleX = (imageSize.x > 0.0f)
                             ? (static_cast<float>(m_renderer.Width()) / imageSize.x)
                             : 1.0f;
    const float scaleY = (imageSize.y > 0.0f)
                             ? (static_cast<float>(m_renderer.Height()) / imageSize.y)
                             : 1.0f;
    const float x = (io.MousePos.x - imageOrigin.x) * scaleX;
    const float y = (io.MousePos.y - imageOrigin.y) * scaleY;

    if (!m_strokeActive) {
        // ストロークを始める前の内容をアンドゥ履歴へ積む。
        // アンドゥの単位は「1 ストローク」で、押しっぱなしの間は 1 段に収まる。
        m_paintMasks.QueueSnapshot(m_device, layer.mask.paint);
        m_strokeActive = true;
        m_strokeLastX = x;
        m_strokeLastY = y;
    }

    compositor::BrushStroke stroke;
    stroke.target = layer.mask.paint;
    stroke.fromX = m_strokeLastX;
    stroke.fromY = m_strokeLastY;
    stroke.toX = x;
    stroke.toY = y;
    stroke.brush = m_brush;
    // 右ドラッグは加算 / 減算を入れ替える。消しゴムへ切り替えずに消せるようにするため。
    stroke.brush.erase = erasePressed ? !m_brush.erase : m_brush.erase;
    m_paintMasks.QueueStroke(stroke);

    m_strokeLastX = x;
    m_strokeLastY = y;
}

// 既定のドックレイアウト。
//
//   +----------+----------------+--------------------+
//   | レイヤー  | ビューポート    | プレビュー設定      |
//   | マテリアル |                |                    |
//   |          |                +--------------------+
//   |          |                | ライティングと露出   |
//   |          |                +--------------------+
//   |          |                | 情報                |
//   +----------+----------------+--------------------+
//
// 比率で組むので、ウィンドウの大きさが変わってもパネルははみ出さない。
void Application::BuildDefaultLayout(ImGuiID dockspaceId) {
    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
    // 分割する前に大きさを入れておかないと、分割比が当てにならない。
    ImGui::DockBuilderSetNodeSize(dockspaceId, ImGui::GetMainViewport()->WorkSize);

    ImGuiID center = dockspaceId;
    ImGuiID left = 0;
    ImGuiID right = 0;
    ImGuiID bottom = 0;
    ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.24f, &left, &center);
    // 残り幅に対する比率。全体では 0.27 ぶんになる。
    ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.355f, &right, &center);
    // アセットの帯。**右カラムを切り出した後の center を割る**ので、
    // 帯はビューポートの真下だけに伸び、右カラムの下へは回り込まない。
    ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.28f, &bottom, &center);

    ImGui::DockBuilderDockWindow("レイヤー", left);
    // マテリアルはレイヤーと同じ枠にタブで並べる。
    // どちらも「何を積むか」を決める作業で、同時には見ない。
    ImGui::DockBuilderDockWindow("マテリアル", left);
    ImGui::DockBuilderDockWindow("ビューポート", center);
    // テクスチャはビューポートの下の帯。マテリアルのマップ欄へドラッグして
    // 割り当てるので、**割り当て先と同時に見えている必要がある。**
    // 左カラムのタブに置くと、マテリアルとテクスチャを同時に出せない。
    ImGui::DockBuilderDockWindow("テクスチャ", bottom);
    // 右カラムは 3 枚をタブで重ねる。縦に積むと 1 枚あたりが短くなり、
    // どれもスクロールしないと全体が見えなくなる。
    ImGui::DockBuilderDockWindow("プレビュー設定", right);
    ImGui::DockBuilderDockWindow("ライティングと露出", right);
    ImGui::DockBuilderDockWindow("情報", right);

    ImGui::DockBuilderFinish(dockspaceId);

    // 前面のタブは「レイヤー」と「プレビュー設定」にする。
    // この時点ではまだウィンドウが無いので、実際の指定は各パネルの Begin 直前で行う。
    m_focusDefaultTabs = 3;
}

void Application::DrawViewportPanel() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    // ホイールでウィンドウがスクロールしないようにする（ズームに使うため）。
    const bool open = ImGui::Begin("ビューポート", nullptr,
                                   ImGuiWindowFlags_NoScrollbar |
                                       ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();

    if (open) {
        const ImVec2 available = ImGui::GetContentRegionAvail();
        // 8 の倍数に丸め、ドラッグ中の作り直しを減らす。
        const auto snap = [](float value) {
            const int clamped = std::clamp(static_cast<int>(value), 64, 4096);
            return static_cast<uint32_t>((clamped / 8) * 8);
        };
        m_requestedViewportWidth = snap(available.x);
        m_requestedViewportHeight = snap(available.y);

        if (m_renderer.HasOutput()) {
            // テクスチャの実サイズではなくコンテンツ領域に合わせて描く。
            // 実サイズで描くとパネルからはみ出し、スクロールバーの出入りで
            // 要求サイズが振動してしまう。作り直しは 1 フレーム遅れる。
            const ImVec2 imageOrigin = ImGui::GetCursorScreenPos();
            ImGui::Image(static_cast<ImTextureID>(m_renderer.OutputHandle().ptr), available);

            // ImGui::Image は入力を消費しないため、そのままだと画像上のドラッグが
            // 「ウィンドウの余白のドラッグ」と解釈されてパネルごと動いてしまう。
            // 同じ矩形に不可視ボタンを重ねてドラッグを受け止める。
            ImGui::SetCursorScreenPos(imageOrigin);
            // ビューポート内に重ねるボタン（表示モード）へ入力を譲る。
            ImGui::SetNextItemAllowOverlap();
            ImGui::InvisibleButton("##viewportInput", available,
                                   ImGuiButtonFlags_MouseButtonLeft |
                                       ImGuiButtonFlags_MouseButtonMiddle |
                                       ImGuiButtonFlags_MouseButtonRight);

            const ImGuiIO& io = ImGui::GetIO();
            renderer::Camera& camera = m_renderer.GetCamera();
            const bool itemActive = ImGui::IsItemActive();
            const bool itemHovered = ImGui::IsItemHovered();

            // L + 左ドラッグはライトの向き。ブラシや軌道より先に見る。
            const bool lightDragging = HandleLightDrag(itemActive);

            // ペイントモードの間は左 / 右ドラッグをブラシが受け取る。
            // 視点操作を残すため、軌道は Alt + 左ドラッグへ移す。
            compositor::MaterialLayer* paintLayer = CurrentPaintLayer();
            const bool brushEnabled = (paintLayer != nullptr) && !io.KeyAlt && !lightDragging;

            if (brushEnabled) {
                HandlePaintInput(*paintLayer, itemActive, imageOrigin, available);
            } else {
                m_strokeActive = false;
            }

            // 視点操作は Alt を押している間だけ受ける（Maya と同じ割り当て）。
            //
            // Alt なしのドラッグは、将来の選択や範囲選択のために空けてある。
            // Alt を押している間はブラシもライトも無効になる（上の brushEnabled と
            // HandleLightDrag が !io.KeyAlt を見る）ので、ここで競合は起きない。
            if (itemActive && io.KeyAlt) {
                if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                    camera.Orbit(io.MouseDelta.x * 0.006f, io.MouseDelta.y * 0.006f);
                } else if (ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
                    camera.Pan(io.MouseDelta.x, io.MouseDelta.y);
                } else if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
                    // 右へ引くと寄る。縦は見ない（斜めに引いたときに暴れるため）。
                    camera.Dolly(io.MouseDelta.x);
                }
            }

            if (itemHovered && io.MouseWheel != 0.0f) {
                camera.Zoom(io.MouseWheel);
            }

            HandleCameraShortcuts(itemHovered);

            const ImVec2 imageMax(imageOrigin.x + available.x, imageOrigin.y + available.y);
            DrawAxisGizmo(camera, imageOrigin, imageMax);
            DrawLightGizmo(imageOrigin, imageMax);

            // ビューポートに重ねる操作。左上に表示モードの切り替えを置く。
            DrawViewportOverlay(imageOrigin);

            // ブラシの当たる範囲を円で示す。半径はビューポートのピクセル単位なので、
            // 表示倍率で割って ImGui の座標へ戻す。
            if (brushEnabled && itemHovered && available.x > 0.0f) {
                const float displayScale =
                    available.x / static_cast<float>(std::max(m_renderer.Width(), 1u));
                const float radius = m_brush.radiusPixels * displayScale;
                ImDrawList* drawList = ImGui::GetWindowDrawList();
                drawList->AddCircle(io.MousePos, radius + 1.0f, IM_COL32(0, 0, 0, 140), 0, 3.0f);
                drawList->AddCircle(io.MousePos, radius, IM_COL32(235, 235, 235, 200), 0, 1.5f);
            }
        }
    }
    ImGui::End();
}

void Application::DrawMaterialPanel() {
    if (m_focusDefaultTabs > 0) {
        ImGui::SetNextWindowFocus();
    }
    if (ImGui::Begin("プレビュー設定")) {
        if (ui::BeginPropertyTable("previewRows")) {
            static const char* const kShapeLabels[] = {"球", "平面", "キューブ"};
            int shape = static_cast<int>(m_renderer.Shape());
            if (ui::PropertyCombo("形状", &shape, kShapeLabels, IM_ARRAYSIZE(kShapeLabels), 0,
                                  "マテリアルを貼って確かめるメッシュ")) {
                m_renderer.Shape() = static_cast<renderer::PreviewShape>(shape);
            }

            ui::PropertyBool("合成結果", &m_renderer.UseMaterialTextures(), true,
                             "オフにすると、レイヤー合成を使わず単色マテリアルで表示する");

            if (m_renderer.UseMaterialTextures()) {
                ui::PropertyFloat("UV スケール", &m_renderer.MaterialUvScale(), 0.25f, 8.0f, 1.0f,
                                  "マテリアルをメッシュ上に何回並べるか", "%.2f", 0, 0.25f);

                ui::PropertyFloat("変位量", &m_renderer.DisplacementScale(), 0.0f, 0.5f, 0.0f,
                                  "ハイトを形状に反映する量（ディスプレイスメント）。"
                                  "0 なら形は変わらない",
                                  "%.2f", 0, 0.01f);

                ui::PropertyBool("テセレーション", &m_renderer.TessellationEnabled(), false,
                                 "画面上の辺が長いところだけメッシュを細かく割る。"
                                 "変位量を上げたときに形がなめらかになる");
                if (m_renderer.TessellationEnabled()) {
                    ui::PropertyFloat("分割の上限", &m_renderer.TessellationFactor(), 1.0f, 16.0f,
                                      8.0f, "1 辺をこの回数まで割る。上げるほど重くなる",
                                      "%.0f", 0, 1.0f);
                }

                int resolution = ResolutionIndex(m_renderer.MaterialResolution());
                if (ui::PropertyCombo("合成解像度", &resolution, kResolutionLabels,
                                      IM_ARRAYSIZE(kResolutionLabels), 1,
                                      "編集中のプレビュー解像度。上げるほど細部が出るが重くなる")) {
                    m_renderer.RequestMaterialResolution(kResolutionValues[resolution]);
                }
            } else {
                renderer::MaterialSettings& material = m_renderer.Material();
                ui::PropertyColor("ベースカラー", &material.baseColor.x,
                                  &kDefaultMaterial.baseColor.x);
                ui::PropertyFloat("ラフネス", &material.roughness, 0.0f, 1.0f,
                                  kDefaultMaterial.roughness, nullptr, "%.2f");
                ui::PropertyFloat("メタルネス", &material.metallic, 0.0f, 1.0f,
                                  kDefaultMaterial.metallic, nullptr, "%.2f");
            }
            ui::EndPropertyTable();
        }

        ui::SectionHeader("カメラ");
        if (ui::BeginPropertyTable("cameraRows")) {
            // 露出を絞り / シャッター / ISO で決めているので、レンズも同じ言葉で扱う。
            // ラジアンのままだと何 mm 相当なのか分からない。
            renderer::Camera& camera = m_renderer.GetCamera();
            float focalLength = renderer::FocalLengthFromFovY(camera.FovY());
            if (ui::PropertyFloat("焦点距離", &focalLength, 12.0f, 200.0f,
                                  renderer::FocalLengthFromFovY(kDefaultCamera.fovY),
                                  "35mm フルサイズ換算。小さいほど広角で、遠近が強く出る",
                                  "%.0f mm", ImGuiSliderFlags_Logarithmic, 1.0f)) {
                camera.FovY() = renderer::FovYFromFocalLength(focalLength);
            }
            ui::PropertyValue("画角", "%.1f 度（垂直）", RadiansToDegrees(camera.FovY()));
            ui::PropertyLabelEmpty("cameraReset");
            if (ui::Button("視点をリセット", ui::kWideButtonWidth)) {
                m_renderer.GetCamera().Reset();
            }
            ui::PropertyEnd();
            ui::EndPropertyTable();
        }
    }
    ImGui::End();
}

void Application::DrawLightingPanel() {
    if (ImGui::Begin("ライティングと露出")) {
        renderer::LightSettings& light = m_renderer.Light();

        ui::SectionHeader("ライト");
        if (ui::BeginPropertyTable("lightRows")) {
            float azimuthDeg = RadiansToDegrees(light.azimuth);
            if (ui::PropertyFloat("方位角", &azimuthDeg, -180.0f, 180.0f,
                                  RadiansToDegrees(kDefaultLight.azimuth),
                                  "太陽の向き（水平方向）", "%.0f 度")) {
                light.azimuth = DegreesToRadians(azimuthDeg);
            }
            float elevationDeg = RadiansToDegrees(light.elevation);
            if (ui::PropertyFloat("仰角", &elevationDeg, -89.0f, 89.0f,
                                  RadiansToDegrees(kDefaultLight.elevation),
                                  "太陽の高さ。低いほど影が伸びる", "%.0f 度")) {
                light.elevation = DegreesToRadians(elevationDeg);
            }
            ui::PropertyFloat("照度", &light.illuminance, 0.0f, 200000.0f,
                              kDefaultLight.illuminance,
                              "lux。晴天の直射日光がおよそ 100000 lux", "%.0f");
            ui::PropertyColor("光の色", &light.color.x, &kDefaultLight.color.x);
            ui::PropertyBool("影", &m_renderer.ShadowEnabled(), true,
                             "ディレクショナルライトの影を落とす。"
                             "ディスプレイスメントで押し出した形にも落ちる");
            ui::EndPropertyTable();
        }

        ui::SectionHeader("露出");
        renderer::ExposureSettings& exposure = m_renderer.Exposure();
        if (ui::BeginPropertyTable("exposureRows")) {
            ui::PropertyBool("EV を直接指定", &exposure.useManualEv, kDefaultExposure.useManualEv,
                             "オフにすると絞り / シャッター / ISO から EV100 を求める");
            if (exposure.useManualEv) {
                ui::PropertyFloat("EV100", &exposure.manualEv100, -6.0f, 20.0f,
                                  kDefaultExposure.manualEv100, nullptr, "%.2f");
            } else {
                ui::PropertyFloat("絞り", &exposure.aperture, 1.0f, 32.0f,
                                  kDefaultExposure.aperture, "F 値。大きいほど暗くなる", "F%.1f");

                float shutterDenominator = 1.0f / exposure.shutterSpeed;
                if (ui::PropertyFloat("シャッター", &shutterDenominator, 1.0f, 4000.0f,
                                      1.0f / kDefaultExposure.shutterSpeed,
                                      "秒の逆数。大きいほど暗くなる", "1/%.0f 秒",
                                      ImGuiSliderFlags_Logarithmic)) {
                    exposure.shutterSpeed = 1.0f / shutterDenominator;
                }
                ui::PropertyFloat("ISO", &exposure.iso, 50.0f, 6400.0f, kDefaultExposure.iso,
                                  "感度。大きいほど明るくなる", "%.0f",
                                  ImGuiSliderFlags_Logarithmic);
            }
            ui::PropertyValue("EV100", "%.2f  (exposure %.3e)", exposure.Ev100(),
                              exposure.Exposure());
            ui::EndPropertyTable();
        }

        ui::SectionHeader("環境 (IBL)");
        if (ui::BeginPropertyTable("iblRows")) {
            ui::PropertyValue("環境", "%s", m_renderer.GetEnvironment().SourceName().c_str());
            ui::PropertyValue("equirect", "%u x %u", m_renderer.GetEnvironment().EquirectWidth(),
                              m_renderer.GetEnvironment().EquirectHeight());
            ui::PropertyFloat("環境光の強さ", &m_renderer.IblIntensity(), 0.0f, 4.0f, 1.0f,
                              nullptr, "%.2f");
            ui::PropertyBool("背景を表示", &m_renderer.ShowSkybox(), true,
                             "オフにすると背景色だけになる。IBL の寄与は残る");

            ui::PropertyLabelEmpty("hdrLoad");
            if (ui::Button("HDRI を開く…", ui::kWideButtonWidth)) {
                const std::filesystem::path path =
                    ShowOpenFileDialog(L"HDRI を開く", HdriFileFilters());
                if (!path.empty()) {
                    m_renderer.RequestHdrLoad(path);
                }
            }
            ui::PropertyEnd();
            ui::EndPropertyTable();
        }

        ui::SectionHeader("手続き的な空");
        renderer::SkySettings& sky = m_renderer.Sky();
        if (ui::BeginPropertyTable("skyRows")) {
            bool skyChanged = false;
            skyChanged |= ui::PropertyColor("天頂色", &sky.zenithColor.x,
                                            &kDefaultSky.zenithColor.x);
            skyChanged |= ui::PropertyColor("地平色", &sky.horizonColor.x,
                                            &kDefaultSky.horizonColor.x);
            skyChanged |= ui::PropertyColor("地面色", &sky.groundColor.x,
                                            &kDefaultSky.groundColor.x);
            skyChanged |= ui::PropertyFloat("輝度", &sky.intensity, 0.0f, 100000.0f,
                                            kDefaultSky.intensity,
                                            "cd/m2。晴天の空はおよそ 4000〜15000", "%.0f");

            ui::PropertyLabelEmpty("skyRebuild");
            const bool rebuild = ui::Button("空に戻す", ui::kWideButtonWidth);
            ui::PropertyEnd();

            if (skyChanged || rebuild) {
                m_renderer.RequestSkyRebuild();
            }
            ui::EndPropertyTable();
        }

        ui::SectionHeader("トーンマップ");
        if (ui::BeginPropertyTable("tonemapRows")) {
            static const char* const kTonemapLabels[] = {"なし", "Reinhard", "ACES"};
            int tonemap = static_cast<int>(m_renderer.Tonemap());
            if (ui::PropertyCombo("方式", &tonemap, kTonemapLabels, IM_ARRAYSIZE(kTonemapLabels),
                                  static_cast<int>(renderer::TonemapMode::Aces))) {
                m_renderer.Tonemap() = static_cast<renderer::TonemapMode>(tonemap);
            }
            ui::EndPropertyTable();
        }
    }
    ImGui::End();
}

// レイヤー一覧。一番上が最前面。ドラッグで並べ替える。
void Application::DrawLayerList() {
    std::vector<compositor::MaterialLayer>& layers = m_materialStack.Layers();
    const auto layerCount = static_cast<int>(layers.size());

    // ドラッグの結果はループの外で反映する。走査中に並びを変えない。
    int dropFrom = -1;
    int dropTo = -1;

    if (ImGui::BeginChild("layerList", ImVec2(0.0f, ui::Scaled(150.0f)),
                          ImGuiChildFlags_Borders)) {
        for (int i = layerCount - 1; i >= 0; --i) {
            compositor::MaterialLayer& layer = layers[static_cast<size_t>(i)];
            ImGui::PushID(i);

            if (ImGui::Checkbox("##enabled", &layer.enabled)) {
                m_materialStack.MarkDirty();
            }
            ImGui::SameLine();
            if (ImGui::Selectable(layer.name.c_str(), m_selectedLayer == i)) {
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

            ImGui::PopID();
        }
    }
    ImGui::EndChild();

    if (dropFrom >= 0 && dropTo >= 0 && dropFrom != dropTo) {
        m_materialStack.MoveTo(static_cast<size_t>(dropFrom), static_cast<size_t>(dropTo));
        m_selectedLayer = dropTo;
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

    if (ui::Button("追加")) {
        compositor::MaterialLayer layer;
        layer.name = "レイヤー " + std::to_string(layers.size() + 1);
        m_materialStack.Add(layer);
        m_selectedLayer = static_cast<int>(layers.size()) - 1;
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
    }
    ImGui::SameLine();
    if (ui::Button("削除") && layerCount > 1) {
        const compositor::PaintMaskId paint =
            layers[static_cast<size_t>(m_selectedLayer)].mask.paint;
        if (paint != compositor::kNoPaintMask) {
            m_paintMasks.Remove(m_device, paint);
        }
        m_materialStack.Remove(static_cast<size_t>(m_selectedLayer));
        m_selectedLayer = std::max(0, m_selectedLayer - 1);
    }

    DrawLayerList();
    ui::HintText("上が最前面。ドラッグで並べ替え");

    if (layerCount == 0) {
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
        }
        // マテリアルを割り当てているときは、見た目はマテリアル側の値で決まる。
        // 同じ意味の値を 2 か所に置くと、どちらが効いているのか分からなくなる。
        const bool hasMaterial = (layer.material != compositor::kNoMaterialAsset);
        if (!hasMaterial) {
            changed |= ui::PropertyColor("ベースカラー", &layer.baseColor.x,
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
        m_materialStack.MarkDirty();
    }

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
            m_paintMasks.Remove(m_device, layer.mask.paint);
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

void Application::DrawMaterialLibraryPanel() {
    if (!ImGui::Begin("マテリアル")) {
        ImGui::End();
        return;
    }

    const std::vector<compositor::MaterialAsset>& assets = m_materialLibrary.Entries();
    const auto assetCount = static_cast<int>(assets.size());
    m_selectedMaterial = std::clamp(m_selectedMaterial, 0, (assetCount > 0) ? assetCount - 1 : 0);

    if (ui::Button("追加")) {
        m_materialLibrary.Add("マテリアル " + std::to_string(assets.size() + 1));
        m_selectedMaterial = static_cast<int>(assets.size()) - 1;
    }
    ImGui::SameLine();
    if (ui::Button("複製") && assetCount > 0) {
        m_materialLibrary.Duplicate(assets[static_cast<size_t>(m_selectedMaterial)]);
        m_selectedMaterial = static_cast<int>(assets.size()) - 1;
    }
    ImGui::SameLine();
    if (ui::Button("削除") && assetCount > 0) {
        const compositor::MaterialAssetId removed =
            assets[static_cast<size_t>(m_selectedMaterial)].id;
        m_materialLibrary.Remove(m_device, removed);
        // 参照していたレイヤーは「なし」へ戻す。無効な ID を残さない。
        for (compositor::MaterialLayer& layer : m_materialStack.Layers()) {
            if (layer.material == removed) {
                layer.material = compositor::kNoMaterialAsset;
            }
        }
        m_materialStack.MarkDirty();
        m_selectedMaterial = std::max(0, m_selectedMaterial - 1);
    }

    // マテリアル単体のファイル (.mmmat)。プロジェクト間で持ち回るために使う。
    // プロジェクトにはマテリアルの構造ごと埋め込まれるので、保存には要らない。
    if (ui::Button("読み込み…", ui::kWideButtonWidth)) {
        const std::filesystem::path path =
            ShowOpenFileDialog(L"マテリアルを読み込む", MaterialFileFilters());
        if (!path.empty()) {
            m_pendingMaterialImport = path;
        }
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(assetCount == 0);
    if (ui::Button("書き出し…", ui::kWideButtonWidth)) {
        const compositor::MaterialAsset& target =
            assets[static_cast<size_t>(m_selectedMaterial)];
        const std::filesystem::path path = ShowSaveFileDialog(
            L"マテリアルを書き出す", MaterialFileFilters(), L"mmmat", FromUtf8(target.name));
        if (!path.empty()) {
            m_pendingMaterialExport = path;
            m_pendingExportMaterial = target.id;
        }
    }
    ImGui::EndDisabled();

    // サムネイルの一覧。パネルの幅に入るだけ横に並べる。
    const float thumbnailSize = ui::Scaled(84.0f);
    if (ImGui::BeginChild("materialGrid", ImVec2(0.0f, ui::Scaled(200.0f)),
                          ImGuiChildFlags_Borders)) {
        const float step = thumbnailSize + ImGui::GetStyle().ItemSpacing.x;
        const auto columns = std::max(1, static_cast<int>(ImGui::GetContentRegionAvail().x / step));

        for (int i = 0; i < assetCount; ++i) {
            const compositor::MaterialAsset& asset = assets[static_cast<size_t>(i)];
            ImGui::PushID(static_cast<int>(asset.id));

            ImGui::BeginGroup();
            const bool selected = (m_selectedMaterial == i);
            // サムネイルは円の外を抜いてあるので、下に敷いた色が四隅から透ける。
            // 選択の手掛かりとしては弱いため、枠を画像の後に重ねる。
            if (asset.thumbnail.IsValid()) {
                ImGui::Image(static_cast<ImTextureID>(asset.thumbnail.srv.gpu.ptr),
                             ImVec2(thumbnailSize, thumbnailSize));
                if (ImGui::IsItemClicked()) {
                    m_selectedMaterial = i;
                }
            } else if (ImGui::Button("##thumbnail", ImVec2(thumbnailSize, thumbnailSize))) {
                m_selectedMaterial = i;
            }
            const bool hovered = ImGui::IsItemHovered();
            ui::ThumbnailFrame(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), selected,
                               hovered);
            if (hovered) {
                ImGui::SetTooltip("%s", asset.name.c_str());
            }
            ImGui::EndGroup();

            ImGui::PopID();
            if (((i + 1) % columns) != 0 && (i + 1) < assetCount) {
                ImGui::SameLine();
            }
        }
    }
    ImGui::EndChild();

    if (assetCount == 0) {
        ui::HintText("「追加」でマテリアルを作り、マップを割り当てる");
        ImGui::End();
        return;
    }

    compositor::MaterialAsset& asset =
        *m_materialLibrary.FindMutable(assets[static_cast<size_t>(m_selectedMaterial)].id);
    bool changed = false;

    ui::SectionHeader("基本");
    if (ui::BeginPropertyTable("materialBasicRows")) {
        char nameBuffer[128] = {};
        std::snprintf(nameBuffer, sizeof(nameBuffer), "%s", asset.name.c_str());
        if (ui::PropertyTextInput("名前", nameBuffer, sizeof(nameBuffer))) {
            asset.name = nameBuffer;
        }

        static const compositor::MaterialAsset kDefaultAsset;
        changed |= ui::PropertyColor("ベースカラー", &asset.baseColorTint.x,
                                     &kDefaultAsset.baseColorTint.x,
                                     "ベースカラーのマップに掛ける色。マップが無ければこの色");
        changed |= ui::PropertyFloat("ラフネス", &asset.roughnessValue, 0.0f, 1.0f,
                                     kDefaultAsset.roughnessValue, "マップが無いときの値",
                                     "%.2f");
        changed |= ui::PropertyFloat("メタルネス", &asset.metallicValue, 0.0f, 1.0f,
                                     kDefaultAsset.metallicValue, "マップが無いときの値",
                                     "%.2f");
        changed |= ui::PropertyFloat("AO", &asset.ambientOcclusionValue, 0.0f, 1.0f,
                                     kDefaultAsset.ambientOcclusionValue, "マップが無いときの値",
                                     "%.2f");
        ui::EndPropertyTable();
    }

    ui::SectionHeader("マップ");
    if (ui::BeginPropertyTable("materialMapRows")) {
        changed |= DrawTextureSlotRow("ベースカラー", asset.baseColor, m_textureLibrary);
        changed |= DrawTextureSlotRow("法線", asset.normal, m_textureLibrary);
        changed |= DrawMapSlotRow("ラフネス", asset.roughness, m_textureLibrary);
        changed |= DrawMapSlotRow("メタルネス", asset.metallic, m_textureLibrary);
        changed |= DrawMapSlotRow("AO", asset.ambientOcclusion, m_textureLibrary);
        changed |= DrawMapSlotRow("ハイト", asset.height, m_textureLibrary);

        // 1 枚に AO / ラフネス / ハイトを詰めたテクスチャをまとめて割り当てる。
        ui::PropertyLabel("ORD", "1 枚に AO / ラフネス / ハイトを詰めたテクスチャ");
        const float ordButtonWidth = ui::Scaled(ui::kButtonWidth);
        const float ordSpacing = ImGui::GetStyle().ItemInnerSpacing.x;
        const float ordComboWidth = std::max(
            ui::Scaled(60.0f),
            std::min(ui::Scaled(ui::kComboMaxWidth), ImGui::GetContentRegionAvail().x) -
                ordButtonWidth - ordSpacing);
        DrawTextureCombo("##ord", m_ordTexture, m_textureLibrary, ordComboWidth);
        ImGui::SameLine(0.0f, ordSpacing);
        ImGui::BeginDisabled(m_ordTexture == compositor::kNoTexture);
        if (ui::Button("割り当て")) {
            m_materialLibrary.AssignOrdTexture(asset.id, m_ordTexture);
            changed = true;
        }
        ImGui::EndDisabled();
        ui::PropertyEnd();

        ui::EndPropertyTable();
    }
    ui::HintText("ORD は AO=R / ラフネス=G / ハイト=B に割り当てる（Megascans の並び）");
    ui::HintText("ハイトはレイヤーの「ハイトのソース」をテクスチャにすると効く");

    if (changed) {
        // サムネイルと合成の両方を作り直す。
        m_materialLibrary.MarkThumbnailDirty(asset.id);
        m_materialStack.MarkDirty();
    }

    ImGui::End();
}

// ファイルメニュー。ここでは要求を積むだけで、実際の読み書きは
// ProcessPendingFileWork がフレームの外で行う（GPU 待機を伴うため）。
void Application::RequestOpenProject() {
    const std::filesystem::path path =
        ShowOpenFileDialog(L"プロジェクトを開く", ProjectFileFilters());
    if (!path.empty()) {
        m_pendingProjectOpen = path;
    }
}

// saveAs が偽でも、まだ一度も保存していなければ保存先を聞く。
void Application::RequestSaveProject(bool saveAs) {
    if (!saveAs && !m_projectPath.empty()) {
        m_pendingProjectSave = m_projectPath;
        return;
    }
    const std::filesystem::path path = ShowSaveFileDialog(
        L"プロジェクトを保存", ProjectFileFilters(), L"mmproj", m_projectPath);
    if (!path.empty()) {
        m_pendingProjectSave = path;
    }
}

// キーボードショートカット。メニューと同じ入口（Request*）を通す。
//
// テキスト入力中でも効かせる（Ctrl + S は入力欄が食う操作ではない）。
// 実際の読み書きはどれも保留されるので、押された時点では要求が積まれるだけ。
void Application::HandleShortcuts() {
    const ImGuiIO& io = ImGui::GetIO();
    if (!io.KeyCtrl || io.KeyAlt) {
        return;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_N, false)) {
        m_pendingProjectNew = true;
    } else if (ImGui::IsKeyPressed(ImGuiKey_O, false)) {
        RequestOpenProject();
    } else if (ImGui::IsKeyPressed(ImGuiKey_S, false)) {
        // Ctrl + Shift + S は「名前を付けて保存」。
        RequestSaveProject(io.KeyShift);
    }
}

// 最近使ったプロジェクト。名前を項目に、置き場所を右の列に出す。
// 同じ名前のプロジェクトが別の場所にあっても見分けられるようにするため。
void Application::DrawRecentMenu() {
    const std::vector<std::filesystem::path>& entries = m_recentProjects.Entries();
    if (!ImGui::BeginMenu("最近使ったプロジェクト", !entries.empty())) {
        return;
    }

    for (size_t i = 0; i < entries.size(); ++i) {
        const std::filesystem::path& path = entries[i];
        ImGui::PushID(static_cast<int>(i));

        const std::string name = ToUtf8(path.filename());
        const std::string directory = ToUtf8(path.parent_path());
        if (ImGui::MenuItem(name.c_str(), directory.c_str())) {
            m_pendingProjectOpen = path;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", ToUtf8(path).c_str());
        }

        ImGui::PopID();
    }

    ImGui::Separator();
    if (ImGui::MenuItem("履歴を消す")) {
        m_recentProjects.Clear();
    }
    ImGui::EndMenu();
}

void Application::DrawFileMenu() {
    if (!ImGui::BeginMenu("ファイル")) {
        return;
    }

    if (ImGui::MenuItem("新規", "Ctrl+N")) {
        m_pendingProjectNew = true;
    }
    if (ImGui::MenuItem("開く…", "Ctrl+O")) {
        RequestOpenProject();
    }
    DrawRecentMenu();
    if (ImGui::MenuItem("保存", "Ctrl+S")) {
        RequestSaveProject(false);
    }
    if (ImGui::MenuItem("名前を付けて保存…", "Ctrl+Shift+S")) {
        RequestSaveProject(true);
    }

    ImGui::Separator();
    if (ImGui::MenuItem("終了")) {
        m_window.RequestClose();
    }
    ImGui::EndMenu();
}

void Application::PushStatus(LogLevel level, const char* text) {
    if (text == nullptr) {
        return;
    }
    m_status.text = text;
    m_status.level = level;
    m_status.time = std::chrono::steady_clock::now();
    m_status.valid = true;
}

// 画面下端のステータスバー。左に直近の通知、右にいま何を持っているか。
//
// メニューバーと同じ仕組み（BeginViewportSideBar）で作業領域を狭めるので、
// **ドックスペースより前に呼ぶこと。** 後だとドックがバーの下へはみ出す。
void Application::DrawStatusBar() {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_NoScrollbar |
                                        ImGuiWindowFlags_NoSavedSettings |
                                        ImGuiWindowFlags_MenuBar;

    if (ImGui::BeginViewportSideBar("##statusBar", viewport, ImGuiDir_Down,
                                    ImGui::GetFrameHeight(), kFlags)) {
        if (ImGui::BeginMenuBar()) {
            // --- 左: いまのモードと直近の通知 -------------------------------
            // モードでビューポートの操作が変わるので、常に見える場所へ出す。
            if (const compositor::MaterialLayer* paintLayer = CurrentPaintLayer();
                paintLayer != nullptr) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_CheckMark));
                ImGui::Text("ペイント中: %s", paintLayer->name.c_str());
                ImGui::PopStyleColor();
                ImGui::TextDisabled("|");
            }

            if (m_status.valid) {
                const auto age = std::chrono::duration<float>(
                                     std::chrono::steady_clock::now() - m_status.time)
                                     .count();
                // 情報は流れて消える。警告とエラーは次の通知まで残す。
                const bool keep = (m_status.level != LogLevel::Info) || (age < kStatusHoldSeconds);
                if (keep) {
                    ImU32 color = ImGui::GetColorU32(ImGuiCol_TextDisabled);
                    if (m_status.level == LogLevel::Warn) {
                        color = ui::WarnColor();
                    } else if (m_status.level == LogLevel::Error) {
                        color = ui::ErrorColor();
                    }
                    ImGui::PushStyleColor(ImGuiCol_Text, color);
                    ImGui::TextUnformatted(m_status.text.c_str());
                    ImGui::PopStyleColor();
                    if (ImGui::IsItemHovered()) {
                        // 長い通知（パスなど）は切れるので、全文はここで読む。
                        ImGui::SetTooltip("%s", m_status.text.c_str());
                    }
                }
            }

            // --- 右: いま何を持っているか -----------------------------------
            const std::string project =
                m_projectPath.empty() ? std::string("未保存のプロジェクト")
                                      : ToUtf8(m_projectPath.filename());
            char summary[320] = {};
            std::snprintf(summary, sizeof(summary),
                          "%s   レイヤー %zu / マテリアル %zu / テクスチャ %zu   合成 %u^2   "
                          "%.0f FPS",
                          project.c_str(), m_materialStack.Layers().size(),
                          m_materialLibrary.Entries().size(), m_textureLibrary.Entries().size(),
                          m_renderer.MaterialResolution(), ImGui::GetIO().Framerate);

            const float summaryWidth = ImGui::CalcTextSize(summary).x;
            const float right = ImGui::GetWindowWidth() - summaryWidth -
                                ImGui::GetStyle().ItemSpacing.x * 2.0f;
            // 通知が長いときは重ねない。右寄せできる余白があるときだけ出す。
            if (right > ImGui::GetCursorPosX()) {
                ImGui::SetCursorPosX(right);
                ImGui::TextDisabled("%s", summary);
            }

            ImGui::EndMenuBar();
        }
    }
    ImGui::End();
}

// このテクスチャを使っている場所を、人が読める形で並べる。
//
// 削除の確認で「どこが壊れるか」を出すために使う。件数だけだと、
// 消していいのか判断できない。
std::vector<std::string> Application::CollectTextureUsers(compositor::TextureId id) const {
    std::vector<std::string> users;
    if (id == compositor::kNoTexture) {
        return users;
    }

    for (const compositor::MaterialAsset& asset : m_materialLibrary.Entries()) {
        const auto add = [&](const char* slotName) {
            users.push_back("マテリアル「" + asset.name + "」の" + slotName);
        };
        if (asset.baseColor == id) {
            add("ベースカラー");
        }
        if (asset.normal == id) {
            add("法線");
        }
        if (asset.roughness.texture == id) {
            add("ラフネス");
        }
        if (asset.metallic.texture == id) {
            add("メタルネス");
        }
        if (asset.ambientOcclusion.texture == id) {
            add("AO");
        }
        if (asset.height.texture == id) {
            add("ハイト");
        }
    }

    for (const compositor::MaterialLayer& layer : m_materialStack.Layers()) {
        if (layer.mask.texture.texture == id) {
            users.push_back("レイヤー「" + layer.name + "」のマスク");
        }
    }
    return users;
}

size_t Application::CountTextureUsers(compositor::TextureId id) const {
    return CollectTextureUsers(id).size();
}

// 参照が残っているテクスチャを消そうとしたときの確認。
//
// 消しても壊れはしない（参照は削除時に「なし」へ落ちる）が、
// **黙って落とすと、どのマテリアルが変わったのか分からなくなる。**
// どこで使われているかを並べて、消すかどうかを決めてもらう。
void Application::DrawTextureRemoveModal() {
    // ビューポート中央に出す。ドックのどこにパネルがあっても同じ位置に出したい。
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (!ImGui::BeginPopupModal(kTextureRemoveModalTitle, nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    const compositor::LibraryTexture* entry = m_textureLibrary.Find(m_textureRemoveCandidate);
    ImGui::Text("「%s」は %zu か所で使われています。",
                (entry != nullptr) ? entry->name.c_str() : "?", m_textureRemoveUsers.size());
    ImGui::Spacing();

    // 使用箇所。多いときは枠を作ってスクロールさせる（窓が縦に伸びきらないように）。
    constexpr size_t kMaxRowsWithoutScroll = 8;
    const bool scroll = m_textureRemoveUsers.size() > kMaxRowsWithoutScroll;
    const float listHeight =
        scroll ? ImGui::GetTextLineHeightWithSpacing() * kMaxRowsWithoutScroll : 0.0f;
    if (ImGui::BeginChild("textureRemoveUsers",
                          ImVec2(ui::Scaled(360.0f), listHeight),
                          ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY)) {
        for (const std::string& user : m_textureRemoveUsers) {
            ImGui::BulletText("%s", user.c_str());
        }
    }
    ImGui::EndChild();

    ImGui::Spacing();
    ui::HintText("削除すると、これらの割り当ては「なし」に戻る");
    ImGui::Spacing();

    const auto close = [this]() {
        m_textureRemoveCandidate = compositor::kNoTexture;
        m_textureRemoveUsers.clear();
        ImGui::CloseCurrentPopup();
    };

    if (ui::Button("削除する", ui::kWideButtonWidth)) {
        m_pendingTextureRemove = m_textureRemoveCandidate;
        close();
    }
    ImGui::SameLine();
    if (ui::Button("やめる", ui::kWideButtonWidth)) {
        close();
    }
    // Esc でも閉じられるようにする。確認はいつでも降りられる方がよい。
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        close();
    }

    ImGui::EndPopup();
}

// 読み込んだ画像の一覧。マテリアルのマップはここから割り当てる。
void Application::DrawTextureLibraryPanel() {
    if (!ImGui::Begin("テクスチャ")) {
        ImGui::End();
        return;
    }

    const std::vector<compositor::LibraryTexture>& entries = m_textureLibrary.Entries();
    const auto textureCount = static_cast<int>(entries.size());
    m_selectedTexture = std::clamp(m_selectedTexture, 0, (textureCount > 0) ? textureCount - 1 : 0);

    if (ui::Button("読み込む…", ui::kWideButtonWidth)) {
        std::vector<std::filesystem::path> paths =
            ShowOpenFilesDialog(L"テクスチャを開く", ImageFileFilters());
        if (!paths.empty()) {
            m_pendingTexturePaths.insert(m_pendingTexturePaths.end(), paths.begin(), paths.end());
        }
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(textureCount == 0);
    if (ui::Button("削除")) {
        const compositor::TextureId target = entries[static_cast<size_t>(m_selectedTexture)].id;
        // マテリアルやレイヤーから参照されているなら、どこが壊れるかを見せて確認する。
        // 参照が無ければ、いちいち止めない。
        m_textureRemoveUsers = CollectTextureUsers(target);
        if (m_textureRemoveUsers.empty()) {
            // 破棄はディスクリプタを返すので、フレームの外で処理する。
            m_pendingTextureRemove = target;
        } else {
            m_textureRemoveCandidate = target;
            ImGui::OpenPopup(kTextureRemoveModalTitle);
        }
    }
    ImGui::EndDisabled();
    DrawTextureRemoveModal();

    // ビューポートの下の横長の帯に置くことを前提に、一覧と詳細を左右に分ける。
    // 縦に積むと、帯の高さでは両方が見えない。
    // 幅が足りないとき（左カラムへドッキングし直したときなど）は縦に積む。
    const float detailWidth = ui::Scaled(300.0f);
    const bool sideBySide = ImGui::GetContentRegionAvail().x > detailWidth * 2.0f;
    const ImVec2 gridSize =
        sideBySide ? ImVec2(-(detailWidth + ImGui::GetStyle().ItemSpacing.x), 0.0f)
                   : ImVec2(0.0f, ui::Scaled(180.0f));

    // サムネイルの一覧。枠の幅に入るだけ横に並べる。
    // 読み込み時にミップを作ってあるので、元の画像をそのまま縮小して出せる。
    const float thumbnailSize = ui::Scaled(72.0f);
    if (ImGui::BeginChild("textureGrid", gridSize, ImGuiChildFlags_Borders)) {
        const float step = thumbnailSize + ImGui::GetStyle().ItemSpacing.x;
        const auto columns = std::max(1, static_cast<int>(ImGui::GetContentRegionAvail().x / step));

        for (int i = 0; i < textureCount; ++i) {
            const compositor::LibraryTexture& entry = entries[static_cast<size_t>(i)];
            ImGui::PushID(static_cast<int>(entry.id));

            ImGui::BeginGroup();
            // **サムネイルは InvisibleButton で ID を持たせてから描く。**
            //
            // ImGui::Image() は ID を持たないアイテムなので、そのままでは
            // BeginDragDropSource() がドラッグを開始できない（黙って false を返す）。
            // ImGuiDragDropFlags_SourceAllowNullID を渡す手もあるが、あれは
            // ウィンドウ相対の位置から ID を捏造するもので、一覧のスクロールや
            // 折り返しで ID が変わってしまう。PushID(entry.id) の下で
            // InvisibleButton を置けば、安定した一意の ID が付く。
            const ImVec2 thumbnailMin = ImGui::GetCursorScreenPos();
            const ImVec2 thumbnailMax(thumbnailMin.x + thumbnailSize,
                                      thumbnailMin.y + thumbnailSize);
            ImGui::InvisibleButton("##thumbnail", ImVec2(thumbnailSize, thumbnailSize));
            const bool hovered = ImGui::IsItemHovered();
            if (ImGui::IsItemClicked()) {
                m_selectedTexture = i;
            }

            // リニアなテクスチャ（EXR）は表示用に焼き直したものを描く。
            // 元のまま描くと極端に暗く、一覧で見分けられない。
            ImGui::GetWindowDrawList()->AddImage(
                static_cast<ImTextureID>(entry.PreviewHandle().ptr), thumbnailMin, thumbnailMax);
            // 選択枠は画像の**後**に描く。背景として敷くと不透明な画像に隠れる。
            ui::ThumbnailFrame(thumbnailMin, thumbnailMax, m_selectedTexture == i, hovered);
            // 読み込んだ直後のものは枠内へ送る。一覧はスクロールするので、
            // 追加しただけでは見えない位置に入ることがある。
            if (m_selectedTexture == i && m_scrollToSelectedTexture) {
                m_scrollToSelectedTexture = false;
                ImGui::SetScrollHereY(1.0f);
            }
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoHoldToOpenOthers)) {
                // マテリアルのマップ欄へ落とすと、そのスロットに割り当たる。
                ImGui::SetDragDropPayload(kTextureDragDropType, &entry.id,
                                          sizeof(compositor::TextureId));
                ImGui::TextUnformatted(entry.name.c_str());
                ImGui::EndDragDropSource();
            } else if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", entry.name.c_str());
            }
            ImGui::EndGroup();

            ImGui::PopID();
            if (((i + 1) % columns) != 0 && (i + 1) < textureCount) {
                ImGui::SameLine();
            }
        }
    }
    ImGui::EndChild();

    if (sideBySide) {
        ImGui::SameLine();
        ImGui::BeginChild("textureDetails", ImVec2(0.0f, 0.0f));
    }

    if (textureCount == 0) {
        ui::HintText("「読み込む…」で画像を読み込む（PNG / JPG / TGA / EXR）");
        if (sideBySide) {
            ImGui::EndChild();
        }
        ImGui::End();
        return;
    }

    const compositor::LibraryTexture& selected = entries[static_cast<size_t>(m_selectedTexture)];
    ui::SectionHeader("選択中");
    if (ui::BeginPropertyTable("textureRows")) {
        char nameBuffer[128] = {};
        std::snprintf(nameBuffer, sizeof(nameBuffer), "%s", selected.name.c_str());
        if (ui::PropertyTextInput("名前", nameBuffer, sizeof(nameBuffer),
                                  "一覧とマップ欄に出る名前。画像ファイルの名前は変わらない")) {
            if (compositor::LibraryTexture* mutableEntry =
                    m_textureLibrary.FindMutable(selected.id);
                mutableEntry != nullptr) {
                mutableEntry->name = nameBuffer;
            }
        }
        ui::PropertyValue("解像度", "%u x %u", selected.texture.width, selected.texture.height);
        ui::PropertyValue("ミップ", "%u 段", selected.texture.mipLevels);
        ui::PropertyValue("形式", "%s", TextureFormatLabel(selected));
        ui::PropertyValue("参照", "%zu か所", CountTextureUsers(selected.id));

        ui::PropertyLabel("場所", "プロジェクトにはここへの相対パスを記録する");
        const std::string directory = ToUtf8(selected.path.parent_path());
        ImGui::TextUnformatted(directory.c_str());
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", ToUtf8(selected.path).c_str());
        }
        ui::PropertyEnd();
        ui::EndPropertyTable();
    }
    ui::HintText("サムネイルをマテリアルのマップ欄へドラッグすると割り当てられる");

    if (sideBySide) {
        ImGui::EndChild();
    }
    ImGui::End();
}

void Application::HandleDroppedFiles(const std::vector<std::filesystem::path>& paths) {
    size_t images = 0;
    for (const std::filesystem::path& path : paths) {
        std::string extension = path.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        // 拡張子で行き先を決める。読み込み自体はどれも保留し、フレームの外で処理する。
        if (extension == ".mmproj") {
            m_pendingProjectOpen = path;
        } else if (extension == ".mmmat") {
            m_pendingMaterialImport = path;
        } else if (extension == ".hdr") {
            m_renderer.RequestHdrLoad(path);
        } else if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" ||
                   extension == ".tga" || extension == ".bmp" || extension == ".exr") {
            m_pendingTexturePaths.push_back(path);
            ++images;
        } else {
            MM_LOG_WARN("扱えない形式です: %s", ToUtf8(path.filename()).c_str());
        }
    }
    if (images > 0) {
        MM_LOG_INFO("%zu 枚の画像を読み込みます", images);
    }
}

void Application::ResetProject() {
    // どれも GPU 待機を伴う。フレームの外から呼ぶこと。
    m_paintMasks.Clear(m_device);
    m_materialLibrary.Clear(m_device);
    m_textureLibrary.Clear(m_device);

    // 既定のスタックへ戻す。MaterialStack を代入で作り直すと revision も 1 へ戻り、
    // 評価器が「変わっていない」と判断してしまうので、中身だけ入れ替える。
    const compositor::MaterialStack defaults;
    m_materialStack.Layers() = defaults.Layers();
    m_materialStack.MarkDirty();

    m_selectedLayer = 0;
    m_selectedMaterial = 0;
    m_selectedTexture = 0;
    m_ordTexture = compositor::kNoTexture;
    m_paintMode = false;
    m_strokeActive = false;
}

void Application::UpdateWindowTitle() {
    std::wstring title;
    if (!m_projectPath.empty()) {
        title = m_projectPath.filename().wstring() + L" - ";
    }
    title += L"Material Mixer";
    m_window.SetTitle(title.c_str());
}

void Application::ProcessPendingFileWork() {
    // どれもリソースの生成・破棄と GPU 待機を伴う。フレームの外で処理すること。

    if (m_pendingProjectNew) {
        m_pendingProjectNew = false;
        ResetProject();
        m_projectPath.clear();
        UpdateWindowTitle();
    }

    if (!m_pendingProjectOpen.empty()) {
        const std::filesystem::path path = m_pendingProjectOpen;
        m_pendingProjectOpen.clear();

        io::ProjectRefs refs{m_materialStack, m_textureLibrary, m_materialLibrary, m_paintMasks,
                             m_renderer};
        if (io::LoadProject(path, m_device, m_pipelineCache, refs)) {
            m_recentProjects.Add(path);
            m_projectPath = path;
            m_selectedLayer = 0;
            m_selectedMaterial = 0;
            m_selectedTexture = 0;
            m_ordTexture = compositor::kNoTexture;
            m_paintMode = false;
            m_strokeActive = false;
            UpdateWindowTitle();
        } else {
            // 消えた / 壊れたプロジェクトを履歴に残しても、選べるだけで意味がない。
            m_recentProjects.Remove(path);
        }
    }

    if (!m_pendingProjectSave.empty()) {
        const std::filesystem::path path = m_pendingProjectSave;
        m_pendingProjectSave.clear();

        io::ProjectRefs refs{m_materialStack, m_textureLibrary, m_materialLibrary, m_paintMasks,
                             m_renderer};
        if (io::SaveProject(path, m_device, refs)) {
            m_recentProjects.Add(path);
            m_projectPath = path;
            UpdateWindowTitle();
        }
    }

    if (!m_pendingMaterialExport.empty()) {
        const std::filesystem::path path = m_pendingMaterialExport;
        const compositor::MaterialAssetId id = m_pendingExportMaterial;
        m_pendingMaterialExport.clear();
        m_pendingExportMaterial = compositor::kNoMaterialAsset;

        if (const compositor::MaterialAsset* asset = m_materialLibrary.Find(id);
            asset != nullptr) {
            io::SaveMaterial(path, *asset, m_textureLibrary);
        }
    }

    if (!m_pendingMaterialImport.empty()) {
        const std::filesystem::path path = m_pendingMaterialImport;
        m_pendingMaterialImport.clear();

        const compositor::MaterialAssetId id = io::LoadMaterial(
            path, m_device, m_pipelineCache, m_textureLibrary, m_materialLibrary);
        if (id != compositor::kNoMaterialAsset) {
            m_selectedMaterial = static_cast<int>(m_materialLibrary.Entries().size()) - 1;
        }
    }

    if (m_pendingTextureRemove != compositor::kNoTexture) {
        const compositor::TextureId removed = m_pendingTextureRemove;
        m_pendingTextureRemove = compositor::kNoTexture;

        // 参照を先に外す。無効な ID を残すと、次に同じ番号が払い出されたときに
        // 別の画像が割り当たってしまう。
        const auto clearSlot = [removed](compositor::TextureId& slot) {
            const bool hit = (slot == removed);
            if (hit) {
                slot = compositor::kNoTexture;
            }
            return hit;
        };
        const auto clearMap = [removed](compositor::MapSlot& slot) {
            const bool hit = (slot.texture == removed);
            if (hit) {
                slot = compositor::MapSlot{};
            }
            return hit;
        };

        for (const compositor::MaterialAsset& entry : m_materialLibrary.Entries()) {
            compositor::MaterialAsset* asset = m_materialLibrary.FindMutable(entry.id);
            bool hit = clearSlot(asset->baseColor);
            hit |= clearSlot(asset->normal);
            hit |= clearMap(asset->roughness);
            hit |= clearMap(asset->metallic);
            hit |= clearMap(asset->ambientOcclusion);
            hit |= clearMap(asset->height);
            if (hit) {
                asset->thumbnailDirty = true;
            }
        }
        for (compositor::MaterialLayer& layer : m_materialStack.Layers()) {
            clearMap(layer.mask.texture);
        }
        clearSlot(m_ordTexture);

        // ディスクリプタを返すので、GPU が読み終わるまで待つ。
        m_device.WaitForGpu();
        m_textureLibrary.Remove(m_device, removed);
        m_materialStack.MarkDirty();
    }
}

float Application::DesiredUiScale() const {
    const io::UiSettings& ui = m_settings.Ui();
    return ui.followSystemScale ? m_imgui.MonitorScale() : ui.manualScale;
}

namespace {

// 拡大率を掛けた大きさ。動画やテクスチャの都合で偶数に丸める。
uint32_t ScaledClientSize(uint32_t base, float scale) {
    const auto scaled = static_cast<uint32_t>(std::lround(static_cast<float>(base) * scale));
    return (scaled + 1u) & ~1u;
}

}  // namespace

uint32_t Application::DefaultClientWidth() const {
    return ScaledClientSize(kInitialWidth, m_imgui.UiScale());
}

uint32_t Application::DefaultClientHeight() const {
    return ScaledClientSize(kInitialHeight, m_imgui.UiScale());
}

// UI を拡大したぶんウィンドウも大きくする。こうすると**作業面積（論理サイズ）が
// 1920x1080 のまま**で、文字と部品だけが大きくなる。
// 拡大率だけ上げるとパネルが窮屈になるので、既定では大きさを揃える。
void Application::ApplyUiScale() {
    const float desired = DesiredUiScale();
    if (std::abs(desired - m_imgui.UiScale()) < 0.001f) {
        return;
    }

    m_imgui.SetUiScale(desired);
    m_window.ResizeClient(DefaultClientWidth(), DefaultClientHeight());
}

// アプリの設定。プロジェクトには保存しない（`%LOCALAPPDATA%` の settings.json）。
//
// ドックへは収めない。常設パネルは「何を作るか」に関わるものだけにして、
// たまにしか触らない設定で作業面積を食わない。
void Application::DrawSettingsWindow() {
    if (!m_showSettings) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(ui::Scaled(460.0f), ui::Scaled(500.0f)),
                             ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("設定", &m_showSettings)) {
        ImGui::End();
        return;
    }

    io::UiSettings& ui = m_settings.Ui();
    bool changed = false;

    ui::SectionHeader("UI");
    if (ui::BeginPropertyTable("settingsUiRows")) {
        ui::PropertyValue("表示スケール", "%.0f%%  (Windows)", m_imgui.MonitorScale() * 100.0f);

        const io::UiSettings defaults;
        changed |= ui::PropertyBool("スケール追従", &ui.followSystemScale,
                                    defaults.followSystemScale,
                                    "Windows の表示スケール（DPI）に UI の大きさを合わせる。"
                                    "切ると常に 100% で描く");

        if (!ui.followSystemScale) {
            static const char* const kScaleLabels[] = {"100%", "125%", "150%", "200%"};
            constexpr float kScaleValues[] = {1.0f, 1.25f, 1.5f, 2.0f};
            int selected = 0;
            for (int i = 0; i < IM_ARRAYSIZE(kScaleValues); ++i) {
                if (std::abs(kScaleValues[i] - ui.manualScale) < 0.01f) {
                    selected = i;
                }
            }
            if (ui::PropertyCombo("拡大率", &selected, kScaleLabels, IM_ARRAYSIZE(kScaleLabels), 0,
                                  "UI の大きさ。ウィンドウの大きさは変わらない")) {
                ui.manualScale = kScaleValues[selected];
                changed = true;
            }
        }
        ui::EndPropertyTable();
    }
    ui::HintText("拡大するとウィンドウも同じ倍率で大きくなる（作業面積は変わらない）");
    ui::HintText("パネルの幅は ini にピクセルで残る。ずれたら 表示 > レイアウトをリセット");

    ui::SectionHeader("ウィンドウ");
    if (ui::BeginPropertyTable("settingsWindowRows")) {
        ui::PropertyValue("描画サイズ", "%u x %u", m_device.Width(), m_device.Height());
        ui::PropertyLabelEmpty("windowReset");
        // 録画やスクリーンショットの解像度を揃えるために、既定へ戻す手段を残す。
        if (ui::Button("既定の大きさに戻す", ui::kWideButtonWidth)) {
            m_window.ResizeClient(DefaultClientWidth(), DefaultClientHeight());
        }
        ui::PropertyEnd();
        ui::EndPropertyTable();
    }
    ui::HintText("既定は %u x %u（%u x %u の %.0f%%）", DefaultClientWidth(),
                 DefaultClientHeight(), kInitialWidth, kInitialHeight,
                 m_imgui.UiScale() * 100.0f);

    ui::SectionHeader("表示");
    if (ui::BeginPropertyTable("settingsDisplayRows")) {
        ui::PropertyBool("垂直同期", &m_vsync, true);
        ui::PropertyBool("ホットリロード", &m_hotReloadEnabled, true,
                         "shaders/ の更新を検出して PSO を作り直す");
        ui::PropertyColor("背景色", m_clearColor, kDefaultClearColor);
        ui::EndPropertyTable();
    }

    if (changed) {
        // 次のフレームの頭で拡大率を反映し、設定を書き出す。
        m_settings.Save();
    }

    ImGui::End();
}

void Application::DrawInfoPanel() {
    if (ImGui::Begin("情報")) {
        const ImGuiIO& io = ImGui::GetIO();

        if (ui::BeginPropertyTable("infoRows")) {
            ui::PropertyValue("バージョン", "%s", MM_APP_VERSION);
            ui::PropertyValue("フレーム", "%.1f FPS (%.3f ms)", io.Framerate,
                              1000.0f / io.Framerate);
            ui::PropertyValue("バックバッファ", "%u x %u", m_device.Width(), m_device.Height());
            ui::PropertyValue("ビューポート", "%u x %u", m_renderer.Width(),
                              m_renderer.Height());
            ui::PropertyValue("合成", "%u^2 / %u レイヤー / %u タイル",
                              m_renderer.MaterialResolution(),
                              m_renderer.Evaluator().EvaluatedLayerCount(),
                              m_renderer.Evaluator().EvaluatedTileCount());
            ui::PropertyValue("ペイント", "%zu 枚 / %u^2 / 履歴 %zu 段", m_paintMasks.Count(),
                              m_paintMasks.Resolution(), m_paintMasks.UndoCount());
            ui::PropertyValue("PSO", "%zu 件", m_pipelineCache.PipelineCount());
            ui::PropertyValue("解放待ち", "%zu 件", m_device.PendingDeletionCount());
            ui::PropertyValue("アップロード", "%llu / %llu KB",
                              static_cast<unsigned long long>(m_device.Upload().PeakBytes() / 1024),
                              static_cast<unsigned long long>(m_device.Upload().BytesPerFrame() /
                                                              1024));
            ui::EndPropertyTable();
        }

    }
    ImGui::End();
}

}  // namespace mm
