#include "app/Application.h"

#include "core/Log.h"

#include <imgui.h>

namespace hm {
namespace {

constexpr uint32_t kInitialWidth = 1600;
constexpr uint32_t kInitialHeight = 900;

#if defined(HM_DEBUG)
constexpr bool kEnableDebugLayer = true;
#else
constexpr bool kEnableDebugLayer = false;
#endif

}  // namespace

bool Application::Initialize() {
    if (!m_window.Create(L"heightmap-mixer", kInitialWidth, kInitialHeight)) {
        return false;
    }

    if (!m_device.Initialize(m_window.Handle(), m_window.Width(), m_window.Height(),
                             kEnableDebugLayer)) {
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

    HM_LOG_INFO("heightmap-mixer %s を起動しました", HM_APP_VERSION);
    return true;
}

void Application::Shutdown() {
    m_device.WaitForGpu();
    m_imgui.Shutdown();
    m_device.Shutdown();
    m_window.Destroy();
}

int Application::Run() {
    while (m_window.PumpMessages()) {
        if (m_window.IsMinimized()) {
            ::WaitMessage();
            continue;
        }

        m_imgui.BeginFrame();
        DrawUi();

        ID3D12GraphicsCommandList* commandList = m_device.BeginFrame(m_clearColor);
        if (commandList == nullptr) {
            // フレームを開始できなかった場合は ImGui の状態を捨てて次へ進む。
            ImGui::EndFrame();
            continue;
        }

        m_imgui.EndFrame(commandList);
        m_device.EndFrame(m_vsync);
    }
    return 0;
}

void Application::DrawUi() {
    ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());

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

    if (ImGui::Begin("情報")) {
        const ImGuiIO& io = ImGui::GetIO();
        ImGui::Text("heightmap-mixer %s", HM_APP_VERSION);
        ImGui::Separator();
        ImGui::Text("%.1f FPS (%.3f ms/frame)", io.Framerate, 1000.0f / io.Framerate);
        ImGui::Text("バックバッファ: %u x %u", m_device.Width(), m_device.Height());
        ImGui::Text("フレームスロット: %u / %u", m_device.FrameIndex(), rhi::kFrameCount);
        ImGui::Separator();
        ImGui::Checkbox("垂直同期", &m_vsync);
        ImGui::ColorEdit3("背景色", m_clearColor);
    }
    ImGui::End();

    if (m_showDemoWindow) {
        ImGui::ShowDemoWindow(&m_showDemoWindow);
    }
}

}  // namespace hm
