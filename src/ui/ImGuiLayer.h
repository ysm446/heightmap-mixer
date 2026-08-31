#pragma once

#include "rhi/Common.h"

namespace hm {

class Window;

namespace rhi {
class Device;
}

// Dear ImGui (docking) の初期化とフレーム制御をまとめる。
class ImGuiLayer {
public:
    ImGuiLayer() = default;
    ~ImGuiLayer();

    ImGuiLayer(const ImGuiLayer&) = delete;
    ImGuiLayer& operator=(const ImGuiLayer&) = delete;

    bool Initialize(Window& window, rhi::Device& device);
    void Shutdown();

    void BeginFrame();
    void EndFrame(ID3D12GraphicsCommandList* commandList);

    // Window のメッセージフックから呼ぶ。true ならウィンドウ側では処理しない。
    bool HandleMessage(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

private:
    void LoadFonts();

    rhi::Device* m_device = nullptr;
    bool m_initialized = false;
};

}  // namespace hm
