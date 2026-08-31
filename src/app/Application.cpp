#include "app/Application.h"

#include "core/Log.h"

#include <imgui.h>

#include <algorithm>
#include <filesystem>
#include <string>

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

// 既定レイアウトを適用するフレーム。
// 1 フレーム目はビューポートの作業領域がまだ確定していないことがあるため、
// 数フレーム待ってから一度だけ配置する。
constexpr uint32_t kDefaultLayoutFrame = 3;

// DPI や画面解像度に依存しないよう、メインビューポートの作業領域に対する比率で置く。
// apply が false のときは何もしない（保存済みレイアウトを尊重する）。
void SetDefaultWindowRect(bool apply, float relativeX, float relativeY, float relativeWidth,
                          float relativeHeight) {
    if (!apply) {
        return;
    }
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 origin = viewport->WorkPos;
    const ImVec2 size = viewport->WorkSize;

    ImGui::SetNextWindowPos(ImVec2(origin.x + size.x * relativeX, origin.y + size.y * relativeY),
                            ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(size.x * relativeWidth, size.y * relativeHeight),
                             ImGuiCond_Always);
}

}  // namespace

bool Application::Initialize() {
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
    if (!m_renderer.Initialize(m_device)) {
        return false;
    }
    if (!m_renderer.Resize(m_device, m_requestedViewportWidth, m_requestedViewportHeight)) {
        return false;
    }

    // ImGui の初期化前に判定する。初期化すると ini が生成されうるため。
    std::error_code layoutCheckError;
    m_applyDefaultLayout =
        !std::filesystem::exists(std::filesystem::path(kImGuiIniFileName), layoutCheckError);

    if (!m_imgui.Initialize(m_window, m_device)) {
        return false;
    }

    m_window.SetMessageHook([this](HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
        return m_imgui.HandleMessage(hwnd, msg, wparam, lparam);
    });
    m_window.SetResizeCallback([this](uint32_t width, uint32_t height) {
        m_device.Resize(width, height);
    });

    HM_LOG_INFO("heightmap-mixer %s を起動しました", HM_APP_VERSION);
    return true;
}

void Application::Shutdown() {
    m_device.WaitForGpu();
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

        m_renderer.Render(m_device, m_pipelineCache, commandList);

        // レンダラがターゲットを差し替えているので、ImGui を描く前に戻す。
        m_device.BindBackBuffer(commandList);
        m_imgui.EndFrame(commandList);
        m_device.EndFrame(m_vsync);
        ++m_frameCounter;
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

    // 既定レイアウトは初回起動時に一度だけ適用する。
    const bool applyLayout = m_applyDefaultLayout && (m_frameCounter == kDefaultLayoutFrame);

    DrawViewportPanel(applyLayout);
    DrawMaterialPanel(applyLayout);
    DrawLightingPanel(applyLayout);
    DrawInfoPanel(applyLayout);

    if (m_showDemoWindow) {
        ImGui::ShowDemoWindow(&m_showDemoWindow);
    }
}

void Application::DrawViewportPanel(bool applyLayout) {
    SetDefaultWindowRect(applyLayout, 0.27f, 0.00f, 0.46f, 0.98f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    const bool open = ImGui::Begin("ビューポート");
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
            ImGui::Image(static_cast<ImTextureID>(m_renderer.OutputHandle().ptr), available);

            // 画像の上でのみカメラ操作を受け付ける。
            if (ImGui::IsItemHovered()) {
                const ImGuiIO& io = ImGui::GetIO();
                renderer::Camera& camera = m_renderer.GetCamera();

                if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                    const ImVec2 delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
                    camera.Orbit(delta.x * 0.006f, delta.y * 0.006f);
                    ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
                }
                if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
                    const ImVec2 delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Middle);
                    camera.Pan(delta.x, delta.y);
                    ImGui::ResetMouseDragDelta(ImGuiMouseButton_Middle);
                }
                if (io.MouseWheel != 0.0f) {
                    camera.Zoom(io.MouseWheel);
                }
            }
        }
    }
    ImGui::End();
}

void Application::DrawMaterialPanel(bool applyLayout) {
    SetDefaultWindowRect(applyLayout, 0.01f, 0.00f, 0.25f, 0.38f);
    if (ImGui::Begin("マテリアル")) {
        static const char* const kShapeLabels[] = {"球", "平面", "キューブ"};
        int shape = static_cast<int>(m_renderer.Shape());
        if (ImGui::Combo("形状", &shape, kShapeLabels, IM_ARRAYSIZE(kShapeLabels))) {
            m_renderer.Shape() = static_cast<renderer::PreviewShape>(shape);
        }

        ImGui::Separator();
        renderer::MaterialSettings& material = m_renderer.Material();
        ImGui::ColorEdit3("ベースカラー", &material.baseColor.x);
        ImGui::SliderFloat("ラフネス", &material.roughness, 0.0f, 1.0f);
        ImGui::SliderFloat("メタルネス", &material.metallic, 0.0f, 1.0f);

        ImGui::Separator();
        if (ImGui::Button("カメラをリセット")) {
            m_renderer.GetCamera().Reset();
        }
        ImGui::SliderFloat("画角 (rad)", &m_renderer.GetCamera().FovY(), 0.2f, 1.5f);
    }
    ImGui::End();
}

void Application::DrawLightingPanel(bool applyLayout) {
    SetDefaultWindowRect(applyLayout, 0.01f, 0.40f, 0.25f, 0.58f);
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

        ImGui::SeparatorText("トーンマップ");
        static const char* const kTonemapLabels[] = {"なし", "Reinhard", "ACES"};
        int tonemap = static_cast<int>(m_renderer.Tonemap());
        if (ImGui::Combo("方式", &tonemap, kTonemapLabels, IM_ARRAYSIZE(kTonemapLabels))) {
            m_renderer.Tonemap() = static_cast<renderer::TonemapMode>(tonemap);
        }
    }
    ImGui::End();
}

void Application::DrawInfoPanel(bool applyLayout) {
    SetDefaultWindowRect(applyLayout, 0.74f, 0.00f, 0.25f, 0.45f);
    if (ImGui::Begin("情報")) {
        const ImGuiIO& io = ImGui::GetIO();
        ImGui::Text("heightmap-mixer %s", HM_APP_VERSION);
        ImGui::Separator();
        ImGui::Text("%.1f FPS (%.3f ms/frame)", io.Framerate, 1000.0f / io.Framerate);
        ImGui::Text("バックバッファ: %u x %u", m_device.Width(), m_device.Height());
        ImGui::Text("ビューポート: %u x %u", m_renderer.Width(), m_renderer.Height());
        ImGui::Text("フレームスロット: %u / %u", m_device.FrameIndex(), rhi::kFrameCount);
        ImGui::Separator();
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
