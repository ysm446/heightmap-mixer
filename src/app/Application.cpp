#include "app/Application.h"

#include "core/FileDialog.h"
#include "core/Log.h"
#include "ui/UiStyle.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <DirectXMath.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace hm {
namespace {

// クライアント領域（描画される中身）のサイズ。ウィンドウ枠は含まない。
// DPI では拡大しない。スクリーンショットや録画の解像度を固定するため。
constexpr uint32_t kInitialWidth = 1920;
constexpr uint32_t kInitialHeight = 1080;

// ホットリロードの走査間隔（フレーム数）。毎フレーム走査するほどの頻度は要らない。
constexpr uint32_t kHotReloadIntervalFrames = 30;

#if defined(HM_DEBUG)
constexpr bool kEnableDebugLayer = true;
#else
constexpr bool kEnableDebugLayer = false;
#endif

// シェーダの探索先。環境変数 HM_SHADER_DIR で差し替えられるようにしておく。
std::filesystem::path ResolveShaderRoot() {
    const DWORD needed = ::GetEnvironmentVariableW(L"HM_SHADER_DIR", nullptr, 0);
    if (needed > 0) {
        std::wstring value;
        value.resize(needed);
        const DWORD written = ::GetEnvironmentVariableW(L"HM_SHADER_DIR", value.data(), needed);
        if (written > 0) {
            value.resize(written);
            return std::filesystem::path(value);
        }
    }
    return std::filesystem::path(HM_SHADER_DIR);
}

float RadiansToDegrees(float radians) {
    return radians * (180.0f / 3.14159265358979323846f);
}

float DegreesToRadians(float degrees) {
    return degrees * (3.14159265358979323846f / 180.0f);
}

}  // namespace

namespace {

// 既定値マーカーが参照する値。数値リテラルではなく設定構造体の初期値を使う。
const compositor::MaterialLayer kDefaultLayer;
const compositor::BrushSettings kDefaultBrush;
const renderer::LightSettings kDefaultLight;
const renderer::ExposureSettings kDefaultExposure;
const renderer::MaterialSettings kDefaultMaterial;
const renderer::SkySettings kDefaultSky;

const char* const kNoiseTypeLabels[] = {"fBm", "尾根状", "セル状"};
const char* const kValueSourceLabels[] = {"定数", "ノイズ", "テクスチャ"};
const char* const kMaskSourceLabels[] = {
    "定数",       "ノイズ",     "テクスチャ", "下地の高さ",
    "下地の傾斜", "下地の曲率", "下地の窪み", "ペイント",
};
const char* const kChannelLabels[] = {"BaseColor", "Normal", "Surface", "Height"};
const char* const kResolutionLabels[] = {"512", "1024", "2048"};
constexpr uint32_t kResolutionValues[] = {512, 1024, 2048};

// レイヤー一覧のドラッグ＆ドロップで使うペイロードの種別。
constexpr const char* kLayerDragDropType = "HM_LAYER";

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
bool DrawNoiseRows(compositor::NoiseParams& noise, const compositor::NoiseParams& defaults) {
    bool changed = DrawNoiseTypeRow("種類", noise.type, defaults.type);
    changed |= ui::PropertyFloat("周波数", &noise.scale, 0.5f, 64.0f, defaults.scale,
                                 "大きいほど細かい模様になる", "%.1f");
    changed |= ui::PropertyFloat("量", &noise.amount, 0.0f, 3.0f, defaults.amount,
                                 "ノイズの寄与。0 で効かなくなる", "%.2f");
    changed |= ui::PropertyInt("オクターブ", &noise.octaves, 1, 8, defaults.octaves,
                               "重ねる段数。多いほど細部が増え、計算も増える");
    changed |= ui::PropertyFloat("オフセット", &noise.offset, 0.0f, 64.0f, defaults.offset,
                                 "同じ設定で別の模様がほしいときにずらす", "%.1f");
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

// テクスチャスロットを選ぶ行。ライブラリの一覧から選ぶ。
bool DrawTextureSlotRow(const char* label, compositor::TextureId& slot,
                        const compositor::TextureLibrary& library) {
    ui::PropertyLabel(label, "「なし」なら定数値を使う");

    std::string preview = "なし";
    if (const compositor::LibraryTexture* current = library.Find(slot); current != nullptr) {
        preview = current->name;
    }

    bool changed = false;
    ImGui::SetNextItemWidth(
        std::min(ui::Scaled(ui::kComboMaxWidth), ImGui::GetContentRegionAvail().x));
    if (ImGui::BeginCombo("##value", preview.c_str())) {
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
    ui::PropertyEnd();
    return changed;
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
        HM_LOG_WARN("COM を初期化できませんでした。ファイル選択ダイアログは使えません");
    }

    // ウィンドウ生成より前に済ませる必要がある。
    ImGuiLayer::EnableDpiAwareness();

    // クライアント領域を実ピクセルで 1920x1080 にする。DPI では拡大しない。
    // UI の大きさは ImGui 側の DPI スケールで合わせる。
    // モニタからはみ出す場合は Window::Create 側で作業領域に収める。
    if (!m_window.Create(L"heightmap-mixer", kInitialWidth, kInitialHeight)) {
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

    m_pendingTexturePaths = options.texturePaths;

    if (!options.hdriPath.empty()) {
        m_renderer.RequestHdrLoad(options.hdriPath);
    }

    HM_LOG_INFO("heightmap-mixer %s を起動しました", HM_APP_VERSION);
    return true;
}

void Application::Shutdown() {
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

    HM_LOG_INFO("シェーダの更新を検出しました。PSO を作り直します");
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
                loaded |= (m_textureLibrary.Load(m_device, m_pipelineCache, path) !=
                           compositor::kNoTexture);
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
    // メニューバーを先に作ることで、メインビューポートの作業領域が
    // メニューバー分を差し引いた状態になる。既定のパネル配置がこれに依存する。
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("ファイル")) {
            ImGui::MenuItem("終了", nullptr, false, false);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("表示")) {
            if (ImGui::MenuItem("レイアウトをリセット")) {
                m_rebuildLayout = true;
            }
            ImGui::Separator();
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
    const ImGuiID dockspaceId = ImGui::GetID("HeightmapMixerDockSpace_v2");

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
    DrawLayerPanel();
    DrawMaterialLibraryPanel();
    DrawMaterialPanel();
    DrawLightingPanel();
    DrawInfoPanel();

    if (m_showDemoWindow) {
        ImGui::ShowDemoWindow(&m_showDemoWindow);
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
    ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.24f, &left, &center);
    // 残り幅に対する比率。全体では 0.27 ぶんになる。
    ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.355f, &right, &center);

    ImGuiID rightBottom = 0;
    ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.28f, &rightBottom, &right);
    ImGuiID rightTop = 0;
    ImGui::DockBuilderSplitNode(right, ImGuiDir_Up, 0.36f, &rightTop, &right);

    ImGui::DockBuilderDockWindow("レイヤー", left);
    // マテリアルはレイヤーと同じ枠にタブで並べる。
    // どちらも「何を積むか」を決める作業で、同時には見ない。
    ImGui::DockBuilderDockWindow("マテリアル", left);
    ImGui::DockBuilderDockWindow("ビューポート", center);
    ImGui::DockBuilderDockWindow("プレビュー設定", rightTop);
    ImGui::DockBuilderDockWindow("ライティングと露出", right);
    ImGui::DockBuilderDockWindow("情報", rightBottom);

    ImGui::DockBuilderFinish(dockspaceId);
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
            ImGui::InvisibleButton("##viewportInput", available,
                                   ImGuiButtonFlags_MouseButtonLeft |
                                       ImGuiButtonFlags_MouseButtonMiddle |
                                       ImGuiButtonFlags_MouseButtonRight);

            const ImGuiIO& io = ImGui::GetIO();
            renderer::Camera& camera = m_renderer.GetCamera();
            const bool itemActive = ImGui::IsItemActive();
            const bool itemHovered = ImGui::IsItemHovered();

            // ペイントモードの間は左 / 右ドラッグをブラシが受け取る。
            // 視点操作を残すため、軌道は Alt + 左ドラッグへ移す。
            compositor::MaterialLayer* paintLayer = CurrentPaintLayer();
            const bool brushEnabled = (paintLayer != nullptr) && !io.KeyAlt;

            if (brushEnabled) {
                HandlePaintInput(*paintLayer, itemActive, imageOrigin, available);
            } else {
                m_strokeActive = false;
            }

            // ブラシが受け取ったドラッグは視点操作に回さない。
            if (itemActive && !m_strokeActive) {
                if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                    camera.Orbit(io.MouseDelta.x * 0.006f, io.MouseDelta.y * 0.006f);
                } else if (ImGui::IsMouseDown(ImGuiMouseButton_Middle) ||
                           ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
                    camera.Pan(io.MouseDelta.x, io.MouseDelta.y);
                }
            }

            if (itemHovered && io.MouseWheel != 0.0f) {
                camera.Zoom(io.MouseWheel);
            }

            DrawAxisGizmo(camera, imageOrigin,
                          ImVec2(imageOrigin.x + available.x, imageOrigin.y + available.y));

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
                                  "マテリアルをメッシュ上に何回並べるか", "%.2f");

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
            ui::PropertyFloat("画角", &m_renderer.GetCamera().FovY(), 0.2f, 1.5f, 0.7853981634f,
                              "垂直方向の画角（ラジアン）。0.785 = 45 度", "%.2f");
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
            if (ui::PropertyFloat("仰角", &elevationDeg, -5.0f, 89.0f,
                                  RadiansToDegrees(kDefaultLight.elevation),
                                  "太陽の高さ。低いほど影が伸びる", "%.0f 度")) {
                light.elevation = DegreesToRadians(elevationDeg);
            }
            ui::PropertyFloat("照度", &light.illuminance, 0.0f, 200000.0f,
                              kDefaultLight.illuminance,
                              "lux。晴天の直射日光がおよそ 100000 lux", "%.0f");
            ui::PropertyColor("光の色", &light.color.x, &kDefaultLight.color.x);
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
                                     "このレイヤーの模様を何回並べるか", "%.2f");
        ui::EndPropertyTable();
    }
    if (layer.material != compositor::kNoMaterialAsset) {
        ui::HintText("色とサーフェスの値はマテリアル側で決まる");
    }

    ui::SectionHeader("ハイト");
    if (ui::BeginPropertyTable("layerHeightRows")) {
        int heightSource = static_cast<int>(layer.heightSource);
        if (ui::PropertyCombo("出どころ", &heightSource, kValueSourceLabels,
                              IM_ARRAYSIZE(kValueSourceLabels),
                              static_cast<int>(kDefaultLayer.heightSource))) {
            layer.heightSource = static_cast<compositor::ValueSource>(heightSource);
            changed = true;
        }
        changed |= ui::PropertyFloat("基準の高さ", &layer.heightBase, -2.0f, 2.0f,
                                     kDefaultLayer.heightBase,
                                     "このレイヤーが「溜まる水位」。下地の高さと比べて勝敗が決まる",
                                     "%.2f");
        if (layer.heightSource == compositor::ValueSource::Noise) {
            changed |= DrawNoiseRows(layer.heightNoise, kDefaultLayer.heightNoise);
        }
        changed |= ui::PropertyFloat("法線の強さ", &layer.normalStrength, 0.0f, 4.0f,
                                     kDefaultLayer.normalStrength,
                                     "ハイトの勾配から作る法線の強さ。0 で平坦", "%.2f");
        ui::EndPropertyTable();
    }

    ui::SectionHeader("マスク");
    if (ui::BeginPropertyTable("layerMaskRows")) {
        int maskSource = static_cast<int>(layer.mask.source);
        if (ui::PropertyCombo("出どころ", &maskSource, kMaskSourceLabels,
                              IM_ARRAYSIZE(kMaskSourceLabels),
                              static_cast<int>(kDefaultLayer.mask.source),
                              "マスクは不透明度として高さと同じ土俵で競合する。"
                              "1.0 にすると高さに関係なく全面を覆う")) {
            layer.mask.source = static_cast<compositor::MaskSource>(maskSource);
            changed = true;
        }
        changed |= ui::PropertyFloat("定数", &layer.mask.constant, 0.0f, 1.0f,
                                     kDefaultLayer.mask.constant,
                                     "出どころの値に掛ける係数", "%.2f");

        if (layer.mask.source == compositor::MaskSource::Texture) {
            changed |= DrawTextureSlotRow("画像", layer.mask.texture, m_textureLibrary);
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

    if (ui::Button("画像を読み込む…", ui::kWideButtonWidth)) {
        std::vector<std::filesystem::path> paths =
            ShowOpenFilesDialog(L"テクスチャを開く", ImageFileFilters());
        if (!paths.empty()) {
            m_pendingTexturePaths.insert(m_pendingTexturePaths.end(), paths.begin(), paths.end());
        }
    }
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("(%zu 枚)", m_textureLibrary.Entries().size());

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
            if (selected) {
                // 選択枠。サムネイルの上に重ねず、背景として敷く。
                ImGui::GetWindowDrawList()->AddRectFilled(
                    ImGui::GetCursorScreenPos(),
                    ImVec2(ImGui::GetCursorScreenPos().x + thumbnailSize,
                           ImGui::GetCursorScreenPos().y + thumbnailSize),
                    ImGui::GetColorU32(ImGuiCol_HeaderActive));
            }
            if (asset.thumbnail.IsValid()) {
                ImGui::Image(static_cast<ImTextureID>(asset.thumbnail.srv.gpu.ptr),
                             ImVec2(thumbnailSize, thumbnailSize));
                if (ImGui::IsItemClicked()) {
                    m_selectedMaterial = i;
                }
            } else if (ImGui::Button("##thumbnail", ImVec2(thumbnailSize, thumbnailSize))) {
                m_selectedMaterial = i;
            }
            if (ImGui::IsItemHovered()) {
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
        changed |= DrawTextureSlotRow("ラフネス", asset.roughness, m_textureLibrary);
        changed |= DrawTextureSlotRow("メタルネス", asset.metallic, m_textureLibrary);
        changed |= DrawTextureSlotRow("AO", asset.ambientOcclusion, m_textureLibrary);
        changed |= DrawTextureSlotRow("ハイト", asset.height, m_textureLibrary);
        ui::EndPropertyTable();
    }
    ui::HintText("ハイトはレイヤーの「ハイトの出どころ」をテクスチャにすると効く");

    if (changed) {
        // サムネイルと合成の両方を作り直す。
        m_materialLibrary.MarkThumbnailDirty(asset.id);
        m_materialStack.MarkDirty();
    }

    ImGui::End();
}

void Application::DrawInfoPanel() {
    if (ImGui::Begin("情報")) {
        const ImGuiIO& io = ImGui::GetIO();

        if (ui::BeginPropertyTable("infoRows")) {
            ui::PropertyValue("バージョン", "%s", HM_APP_VERSION);
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

        ui::SectionHeader("表示");
        if (ui::BeginPropertyTable("displayRows")) {
            ui::PropertyBool("垂直同期", &m_vsync, true);
            ui::PropertyBool("ホットリロード", &m_hotReloadEnabled, true,
                             "shaders/ の更新を検出して PSO を作り直す");
            ui::PropertyColor("背景色", m_clearColor, kDefaultClearColor);
            ui::EndPropertyTable();
        }
    }
    ImGui::End();
}

}  // namespace hm
