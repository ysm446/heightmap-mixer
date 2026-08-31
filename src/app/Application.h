#pragma once

#include "core/Window.h"
#include "rhi/Device.h"
#include "ui/ImGuiLayer.h"

namespace hm {

// アプリ本体。ウィンドウ、デバイス、UI の生存期間とフレームループを持つ。
class Application {
public:
    bool Initialize();
    void Shutdown();
    int Run();

private:
    void DrawUi();

    Window m_window;
    rhi::Device m_device;
    ImGuiLayer m_imgui;

    float m_clearColor[4] = {0.09f, 0.09f, 0.11f, 1.0f};
    bool m_vsync = true;
    bool m_showDemoWindow = false;
};

}  // namespace hm
