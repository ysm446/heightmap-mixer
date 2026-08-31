#include "app/Application.h"

#include "core/Log.h"

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace hm {
namespace {

constexpr uint32_t kInitialWidth = 1600;
constexpr uint32_t kInitialHeight = 900;

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

// 既定レイアウトを適用し始めるフレーム。
// 1 フレーム目はビューポートの作業領域がまだ確定していないことがあるため、
// 数フレーム待ってから適用する。
constexpr uint32_t kDefaultLayoutFrame = 3;

// DPI や画面解像度に依存しないよう、メインビューポートの作業領域に対する比率で置く。
//
// 条件は ImGuiCond_FirstUseEver。保存済みレイアウト(ini)にあるウィンドウには効かず、
// 新しく追加したウィンドウだけが既定位置に置かれる。
// 「ini が無い初回起動だけ適用する」方式にすると、後から増やしたパネルが
// 既存 ini の環境で配置されないまま重なってしまう。
void SetDefaultWindowRect(bool apply, float relativeX, float relativeY, float relativeWidth,
                          float relativeHeight) {
    if (!apply) {
        return;
    }
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 origin = viewport->WorkPos;
    const ImVec2 size = viewport->WorkSize;

    ImGui::SetNextWindowPos(ImVec2(origin.x + size.x * relativeX, origin.y + size.y * relativeY),
                            ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(size.x * relativeWidth, size.y * relativeHeight),
                             ImGuiCond_FirstUseEver);
}

}  // namespace

// ノイズの種類を選ぶコンボ。
static bool DrawNoiseTypeCombo(const char* label, compositor::NoiseType& type) {
    static const char* const kLabels[] = {"fBm", "尾根状", "セル状"};
    int selected = static_cast<int>(type);
    if (ImGui::Combo(label, &selected, kLabels, IM_ARRAYSIZE(kLabels))) {
        type = static_cast<compositor::NoiseType>(selected);
        return true;
    }
    return false;
}

// テクスチャスロットの選択 UI。ライブラリの一覧からコンボで選ぶ。
static bool DrawTextureSlot(const char* label, compositor::TextureId& slot,
                            const compositor::TextureLibrary& library) {
    const std::vector<compositor::LibraryTexture>& entries = library.Entries();

    std::string preview = "なし";
    if (const compositor::LibraryTexture* current = library.Find(slot); current != nullptr) {
        preview = current->name;
    }

    bool changed = false;
    if (ImGui::BeginCombo(label, preview.c_str())) {
        if (ImGui::Selectable("なし", slot == compositor::kNoTexture)) {
            slot = compositor::kNoTexture;
            changed = true;
        }
        for (const compositor::LibraryTexture& entry : entries) {
            ImGui::PushID(static_cast<int>(entry.id));
            if (ImGui::Selectable(entry.name.c_str(), slot == entry.id)) {
                slot = entry.id;
                changed = true;
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    return changed;
}

bool Application::Initialize(const StartupOptions& options) {
    m_options = options;

    // ウィンドウ生成より前に済ませる必要がある。
    ImGuiLayer::EnableDpiAwareness();

    // DPI 対応後はウィンドウサイズが物理ピクセルになるため、
    // 初期サイズも DPI に合わせないと高 DPI 環境で相対的に小さくなる。
    // モニタからはみ出す場合は Window::Create 側で作業領域に収める。
    const float systemDpiScale = static_cast<float>(::GetDpiForSystem()) / 96.0f;
    const auto initialWidth = static_cast<uint32_t>(kInitialWidth * systemDpiScale);
    const auto initialHeight = static_cast<uint32_t>(kInitialHeight * systemDpiScale);

    if (!m_window.Create(L"heightmap-mixer", initialWidth, initialHeight)) {
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

    for (const std::filesystem::path& texturePath : options.texturePaths) {
        m_textureLibrary.Load(m_device, m_pipelineCache, texturePath);
    }
    if (!options.texturePaths.empty()) {
        m_materialStack.MarkDirty();
    }

    if (!options.hdriPath.empty()) {
        m_renderer.RequestHdrLoad(options.hdriPath);
    }

    HM_LOG_INFO("heightmap-mixer %s を起動しました", HM_APP_VERSION);
    return true;
}

void Application::Shutdown() {
    m_device.WaitForGpu();
    m_textureLibrary.Destroy(m_device);
    m_renderer.Shutdown(m_device);
    m_imgui.Shutdown();
    m_pipelineCache.Destroy();
    m_shaderCompiler.Destroy();
    m_device.Shutdown();
    m_window.Destroy();
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

        // テクスチャ読み込みも GPU 待機を伴うため、フレームの外で処理する。
        if (!m_pendingTexturePath.empty()) {
            const std::filesystem::path path = m_pendingTexturePath;
            m_pendingTexturePath.clear();
            if (m_textureLibrary.Load(m_device, m_pipelineCache, path) !=
                compositor::kNoTexture) {
                m_materialStack.MarkDirty();
            }
        }

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

        m_renderer.Render(m_device, m_pipelineCache, commandList, m_materialStack,
                          m_textureLibrary);

        // レンダラがターゲットを差し替えているので、ImGui を描く前に戻す。
        m_device.BindBackBuffer(commandList);
        m_imgui.EndFrame(commandList);
        m_device.EndFrame(m_vsync);
        ++m_frameCounter;

        // 開発用のスクリーンショット。書き出したら終了する。
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
            ImGui::MenuItem("ImGui デモ", nullptr, &m_showDemoWindow);
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());

    // ビューポートの作業領域が確定してから既定レイアウトを流す。
    // 実際に効くのは ini に無いウィンドウだけ（FirstUseEver）。
    const bool applyLayout = m_frameCounter >= kDefaultLayoutFrame;

    DrawViewportPanel(applyLayout);
    DrawLayerPanel(applyLayout);
    DrawMaterialPanel(applyLayout);
    DrawLightingPanel(applyLayout);
    DrawInfoPanel(applyLayout);

    if (m_showDemoWindow) {
        ImGui::ShowDemoWindow(&m_showDemoWindow);
    }
}

void Application::DrawViewportPanel(bool applyLayout) {
    SetDefaultWindowRect(applyLayout, 0.27f, 0.00f, 0.44f, 0.98f);
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

            if (ImGui::IsItemActive()) {
                if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                    camera.Orbit(io.MouseDelta.x * 0.006f, io.MouseDelta.y * 0.006f);
                } else if (ImGui::IsMouseDown(ImGuiMouseButton_Middle) ||
                           ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
                    camera.Pan(io.MouseDelta.x, io.MouseDelta.y);
                }
            }

            if (ImGui::IsItemHovered() && io.MouseWheel != 0.0f) {
                camera.Zoom(io.MouseWheel);
            }
        }
    }
    ImGui::End();
}

void Application::DrawMaterialPanel(bool applyLayout) {
    SetDefaultWindowRect(applyLayout, 0.72f, 0.00f, 0.275f, 0.31f);
    if (ImGui::Begin("プレビュー設定")) {
        static const char* const kShapeLabels[] = {"球", "平面", "キューブ"};
        int shape = static_cast<int>(m_renderer.Shape());
        if (ImGui::Combo("形状", &shape, kShapeLabels, IM_ARRAYSIZE(kShapeLabels))) {
            m_renderer.Shape() = static_cast<renderer::PreviewShape>(shape);
        }

        ImGui::Separator();
        ImGui::Checkbox("合成結果を使う", &m_renderer.UseMaterialTextures());
        if (m_renderer.UseMaterialTextures()) {
            ImGui::SliderFloat("UV スケール", &m_renderer.MaterialUvScale(), 0.25f, 8.0f);

            static const char* const kResolutionLabels[] = {"512", "1024", "2048"};
            static const uint32_t kResolutionValues[] = {512, 1024, 2048};
            int selected = 1;
            for (int i = 0; i < IM_ARRAYSIZE(kResolutionValues); ++i) {
                if (kResolutionValues[i] == m_renderer.MaterialResolution()) {
                    selected = i;
                    break;
                }
            }
            if (ImGui::Combo("合成解像度", &selected, kResolutionLabels,
                             IM_ARRAYSIZE(kResolutionLabels))) {
                m_renderer.RequestMaterialResolution(kResolutionValues[selected]);
            }
        } else {
            // 合成を使わないときの単色マテリアル。
            renderer::MaterialSettings& material = m_renderer.Material();
            ImGui::ColorEdit3("ベースカラー", &material.baseColor.x);
            ImGui::SliderFloat("ラフネス", &material.roughness, 0.0f, 1.0f);
            ImGui::SliderFloat("メタルネス", &material.metallic, 0.0f, 1.0f);
        }

        ImGui::Separator();
        if (ImGui::Button("カメラをリセット")) {
            m_renderer.GetCamera().Reset();
        }
        ImGui::SliderFloat("画角 (rad)", &m_renderer.GetCamera().FovY(), 0.2f, 1.5f);
    }
    ImGui::End();
}

void Application::DrawLightingPanel(bool applyLayout) {
    SetDefaultWindowRect(applyLayout, 0.72f, 0.32f, 0.275f, 0.45f);
    if (ImGui::Begin("ライティングと露出")) {
        renderer::LightSettings& light = m_renderer.Light();

        ImGui::SeparatorText("ライト");
        float azimuthDeg = RadiansToDegrees(light.azimuth);
        if (ImGui::SliderFloat("方位角", &azimuthDeg, -180.0f, 180.0f, "%.0f 度")) {
            light.azimuth = DegreesToRadians(azimuthDeg);
        }
        float elevationDeg = RadiansToDegrees(light.elevation);
        if (ImGui::SliderFloat("仰角", &elevationDeg, -5.0f, 89.0f, "%.0f 度")) {
            light.elevation = DegreesToRadians(elevationDeg);
        }
        ImGui::DragFloat("照度 (lux)", &light.illuminance, 500.0f, 0.0f, 200000.0f, "%.0f");
        ImGui::ColorEdit3("光の色", &light.color.x);
        ImGui::TextDisabled("晴天の直射日光 = 約 100000 lux");

        ImGui::SeparatorText("露出");
        renderer::ExposureSettings& exposure = m_renderer.Exposure();
        ImGui::Checkbox("EV を直接指定", &exposure.useManualEv);
        if (exposure.useManualEv) {
            ImGui::SliderFloat("EV100", &exposure.manualEv100, -6.0f, 20.0f, "%.2f");
        } else {
            ImGui::SliderFloat("絞り (F)", &exposure.aperture, 1.0f, 32.0f, "F%.1f");

            float shutterDenominator = 1.0f / exposure.shutterSpeed;
            if (ImGui::SliderFloat("シャッター", &shutterDenominator, 1.0f, 4000.0f, "1/%.0f 秒",
                                   ImGuiSliderFlags_Logarithmic)) {
                exposure.shutterSpeed = 1.0f / shutterDenominator;
            }
            ImGui::SliderFloat("ISO", &exposure.iso, 50.0f, 6400.0f, "%.0f",
                               ImGuiSliderFlags_Logarithmic);
        }
        ImGui::Text("EV100 = %.2f  /  exposure = %.3e", exposure.Ev100(), exposure.Exposure());

        ImGui::SeparatorText("環境 (IBL)");
        ImGui::Text("環境: %s", m_renderer.GetEnvironment().SourceName().c_str());
        ImGui::Text("equirect: %u x %u", m_renderer.GetEnvironment().EquirectWidth(),
                    m_renderer.GetEnvironment().EquirectHeight());
        ImGui::SliderFloat("環境光の強さ", &m_renderer.IblIntensity(), 0.0f, 4.0f);
        ImGui::Checkbox("背景を表示", &m_renderer.ShowSkybox());

        renderer::SkySettings& sky = m_renderer.Sky();
        bool skyChanged = false;
        skyChanged |= ImGui::ColorEdit3("天頂色", &sky.zenithColor.x);
        skyChanged |= ImGui::ColorEdit3("地平色", &sky.horizonColor.x);
        skyChanged |= ImGui::ColorEdit3("地面色", &sky.groundColor.x);
        skyChanged |= ImGui::DragFloat("空の輝度 (cd/m2)", &sky.intensity, 50.0f, 0.0f, 100000.0f,
                                       "%.0f");
        if (skyChanged || ImGui::Button("手続き的な空に戻す")) {
            m_renderer.RequestSkyRebuild();
        }

        ImGui::Spacing();
        ImGui::SetNextItemWidth(-120.0f);
        ImGui::InputText("HDRI のパス", m_hdrPathBuffer, IM_ARRAYSIZE(m_hdrPathBuffer));
        if (ImGui::Button("HDRI を読み込む") && m_hdrPathBuffer[0] != 0) {
            m_renderer.RequestHdrLoad(std::filesystem::path(m_hdrPathBuffer));
        }
        ImGui::TextDisabled("Radiance HDR (.hdr) に対応");

        ImGui::SeparatorText("トーンマップ");
        static const char* const kTonemapLabels[] = {"なし", "Reinhard", "ACES"};
        int tonemap = static_cast<int>(m_renderer.Tonemap());
        if (ImGui::Combo("方式", &tonemap, kTonemapLabels, IM_ARRAYSIZE(kTonemapLabels))) {
            m_renderer.Tonemap() = static_cast<renderer::TonemapMode>(tonemap);
        }
    }
    ImGui::End();
}

void Application::DrawLayerPanel(bool applyLayout) {
    SetDefaultWindowRect(applyLayout, 0.005f, 0.00f, 0.26f, 0.98f);
    if (!ImGui::Begin("レイヤー")) {
        ImGui::End();
        return;
    }

    std::vector<compositor::MaterialLayer>& layers = m_materialStack.Layers();
    const auto layerCount = static_cast<int>(layers.size());
    m_selectedLayer = std::clamp(m_selectedLayer, 0, (layerCount > 0) ? layerCount - 1 : 0);

    if (ImGui::Button("追加")) {
        compositor::MaterialLayer layer;
        layer.name = "レイヤー " + std::to_string(layers.size() + 1);
        m_materialStack.Add(layer);
        m_selectedLayer = static_cast<int>(layers.size()) - 1;
    }
    ImGui::SameLine();
    if (ImGui::Button("複製") && layerCount > 0) {
        compositor::MaterialLayer copy = layers[static_cast<size_t>(m_selectedLayer)];
        copy.name += " のコピー";
        m_materialStack.Add(copy);
        m_selectedLayer = static_cast<int>(layers.size()) - 1;
    }
    ImGui::SameLine();
    if (ImGui::Button("削除") && layerCount > 1) {
        m_materialStack.Remove(static_cast<size_t>(m_selectedLayer));
        m_selectedLayer = std::max(0, m_selectedLayer - 1);
    }

    if (ImGui::Button("上へ")) {
        // 一覧は上が最前面なので、表示上の「上へ」はスタックでは後ろへ動かす。
        m_materialStack.Move(static_cast<size_t>(m_selectedLayer), 1);
        m_selectedLayer = std::min(m_selectedLayer + 1, layerCount - 1);
    }
    ImGui::SameLine();
    if (ImGui::Button("下へ")) {
        m_materialStack.Move(static_cast<size_t>(m_selectedLayer), -1);
        m_selectedLayer = std::max(m_selectedLayer - 1, 0);
    }

    ImGui::Separator();

    // 一覧は上が最前面になるよう逆順に並べる。
    if (ImGui::BeginChild("layerList", ImVec2(0.0f, 160.0f * m_imgui.DpiScale()),
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
            ImGui::PopID();
        }
    }
    ImGui::EndChild();

    if (layerCount == 0) {
        ImGui::End();
        return;
    }

    compositor::MaterialLayer& layer = layers[static_cast<size_t>(m_selectedLayer)];
    bool changed = false;

    ImGui::SeparatorText("基本");
    char nameBuffer[128] = {};
    std::snprintf(nameBuffer, sizeof(nameBuffer), "%s", layer.name.c_str());
    if (ImGui::InputText("名前", nameBuffer, sizeof(nameBuffer))) {
        layer.name = nameBuffer;
    }
    changed |= ImGui::ColorEdit3("ベースカラー", &layer.baseColor.x);
    changed |= ImGui::SliderFloat("ラフネス", &layer.roughness, 0.0f, 1.0f);
    changed |= ImGui::SliderFloat("メタルネス", &layer.metallic, 0.0f, 1.0f);
    changed |= ImGui::SliderFloat("AO", &layer.ambientOcclusion, 0.0f, 1.0f);
    changed |= ImGui::SliderFloat("UV スケール", &layer.uvScale, 0.25f, 16.0f);

    ImGui::SeparatorText("ハイト");
    static const char* const kSourceLabels[] = {"定数", "ノイズ", "テクスチャ"};
    int heightSource = static_cast<int>(layer.heightSource);
    if (ImGui::Combo("ハイトの出どころ", &heightSource, kSourceLabels,
                     IM_ARRAYSIZE(kSourceLabels))) {
        layer.heightSource = static_cast<compositor::ValueSource>(heightSource);
        changed = true;
    }
    changed |= ImGui::SliderFloat("基準の高さ", &layer.heightBase, -2.0f, 2.0f);
    if (layer.heightSource == compositor::ValueSource::Noise) {
        changed |= DrawNoiseTypeCombo("ノイズの種類##h", layer.heightNoise.type);
        changed |= ImGui::SliderFloat("周波数##h", &layer.heightNoise.scale, 0.5f, 64.0f);
        changed |= ImGui::SliderFloat("量##h", &layer.heightNoise.amount, 0.0f, 3.0f);
        changed |= ImGui::SliderInt("オクターブ##h", &layer.heightNoise.octaves, 1, 8);
        changed |= ImGui::SliderFloat("オフセット##h", &layer.heightNoise.offset, 0.0f, 64.0f);
    }
    changed |= ImGui::SliderFloat("法線の強さ", &layer.normalStrength, 0.0f, 4.0f);

    ImGui::SeparatorText("マスク");
    static const char* const kMaskSourceLabels[] = {
        "定数", "ノイズ", "テクスチャ", "下地の高さ", "下地の傾斜", "下地の曲率", "下地の窪み",
    };
    int maskSource = static_cast<int>(layer.mask.source);
    if (ImGui::Combo("マスクの出どころ", &maskSource, kMaskSourceLabels,
                     IM_ARRAYSIZE(kMaskSourceLabels))) {
        layer.mask.source = static_cast<compositor::MaskSource>(maskSource);
        changed = true;
    }
    if (m_selectedLayer == 0) {
        ImGui::TextDisabled("一番下のレイヤーは下地なのでマスクは効かない");
    }

    changed |= ImGui::SliderFloat("定数##m", &layer.mask.constant, 0.0f, 1.0f);

    if (layer.mask.source == compositor::MaskSource::Noise) {
        changed |= DrawNoiseTypeCombo("ノイズの種類##m", layer.mask.noise.type);
        changed |= ImGui::SliderFloat("周波数##m", &layer.mask.noise.scale, 0.5f, 64.0f);
        changed |= ImGui::SliderFloat("量##m", &layer.mask.noise.amount, 0.0f, 2.0f);
        changed |= ImGui::SliderInt("オクターブ##m", &layer.mask.noise.octaves, 1, 8);
        changed |= ImGui::SliderFloat("オフセット##m", &layer.mask.noise.offset, 0.0f, 64.0f);
    }
    if (compositor::IsDerivedMaskSource(layer.mask.source)) {
        changed |= ImGui::SliderFloat("強調##m", &layer.mask.derivedScale, 0.0f, 8.0f);
        switch (layer.mask.source) {
            case compositor::MaskSource::Slope:
                ImGui::TextDisabled("急な面ほど 1 に近づく");
                break;
            case compositor::MaskSource::Curvature:
                ImGui::TextDisabled("0.5 が平坦。凸で大、凹で小");
                break;
            case compositor::MaskSource::Cavity:
                ImGui::TextDisabled("窪んでいるほど 1 に近づく");
                break;
            default:
                ImGui::TextDisabled("下地が高いほど 1 に近づく");
                break;
        }
    }

    changed |= ImGui::SliderFloat("カーブ", &layer.mask.contrast, 0.0f, 4.0f);
    changed |= ImGui::SliderFloat("レベル下限", &layer.mask.levelsLow, 0.0f, 1.0f);
    changed |= ImGui::SliderFloat("レベル上限", &layer.mask.levelsHigh, 0.0f, 1.0f);
    changed |= ImGui::Checkbox("反転", &layer.mask.invert);

    ImGui::SeparatorText("テクスチャ");
    ImGui::SetNextItemWidth(-90.0f);
    ImGui::InputText("パス", m_texturePathBuffer, IM_ARRAYSIZE(m_texturePathBuffer));
    if (ImGui::Button("読み込む") && m_texturePathBuffer[0] != 0) {
        m_pendingTexturePath = std::filesystem::path(m_texturePathBuffer);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(%zu 枚)", m_textureLibrary.Entries().size());

    changed |= DrawTextureSlot("ベースカラー##t", layer.textures.baseColor, m_textureLibrary);
    changed |= DrawTextureSlot("法線##t", layer.textures.normal, m_textureLibrary);
    changed |= DrawTextureSlot("ラフネス##t", layer.textures.roughness, m_textureLibrary);
    changed |= DrawTextureSlot("メタルネス##t", layer.textures.metallic, m_textureLibrary);
    changed |= DrawTextureSlot("AO##t", layer.textures.ambientOcclusion, m_textureLibrary);
    changed |= DrawTextureSlot("ハイト##t", layer.textures.height, m_textureLibrary);
    changed |= DrawTextureSlot("マスク##t", layer.textures.mask, m_textureLibrary);
    ImGui::TextDisabled("ハイト / マスクは出どころを「テクスチャ」にすると有効");

    ImGui::SeparatorText("合成");
    changed |= ImGui::SliderFloat("境界の柔らかさ", &layer.blendRange, 0.0f, 1.0f);
    ImGui::TextDisabled("0 に近いほど硬い置き換えになる");

    ImGui::Text("書き込むチャンネル");
    static const char* const kChannelLabels[] = {"BaseColor", "Normal", "Surface", "Height"};
    for (uint32_t i = 0; i < 4; ++i) {
        bool enabled = (layer.channelMask & (1u << i)) != 0u;
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::Checkbox(kChannelLabels[i], &enabled)) {
            layer.channelMask = enabled ? (layer.channelMask | (1u << i))
                                        : (layer.channelMask & ~(1u << i));
            changed = true;
        }
        ImGui::PopID();
        if (i != 3) {
            ImGui::SameLine();
        }
    }

    if (changed) {
        m_materialStack.MarkDirty();
    }

    ImGui::End();
}

void Application::DrawInfoPanel(bool applyLayout) {
    SetDefaultWindowRect(applyLayout, 0.72f, 0.78f, 0.275f, 0.20f);
    if (ImGui::Begin("情報")) {
        const ImGuiIO& io = ImGui::GetIO();
        ImGui::Text("heightmap-mixer %s", HM_APP_VERSION);
        ImGui::Separator();
        ImGui::Text("%.1f FPS (%.3f ms/frame)", io.Framerate, 1000.0f / io.Framerate);
        ImGui::Text("バックバッファ: %u x %u", m_device.Width(), m_device.Height());
        ImGui::Text("ビューポート: %u x %u", m_renderer.Width(), m_renderer.Height());
        ImGui::Text("フレームスロット: %u / %u", m_device.FrameIndex(), rhi::kFrameCount);
        ImGui::Separator();
        ImGui::Text("合成: %u x %u / %u レイヤー / %u タイル",
                    m_renderer.MaterialResolution(), m_renderer.MaterialResolution(),
                    m_renderer.Evaluator().EvaluatedLayerCount(),
                    m_renderer.Evaluator().EvaluatedTileCount());
        ImGui::Text("PSO キャッシュ: %zu 件", m_pipelineCache.PipelineCount());
        ImGui::Text("解放待ち: %zu 件", m_device.PendingDeletionCount());
        ImGui::Text("アップロード最大使用量: %llu KB / %llu KB",
                    static_cast<unsigned long long>(m_device.Upload().PeakBytes() / 1024),
                    static_cast<unsigned long long>(m_device.Upload().BytesPerFrame() / 1024));
        ImGui::Separator();
        ImGui::Checkbox("垂直同期", &m_vsync);
        ImGui::Checkbox("シェーダのホットリロード", &m_hotReloadEnabled);
        ImGui::ColorEdit3("背景色", m_clearColor);
    }
    ImGui::End();
}

}  // namespace hm
