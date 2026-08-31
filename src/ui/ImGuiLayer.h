#pragma once

#include "rhi/Common.h"

namespace hm {

// ImGui のレイアウト保存先。作業ディレクトリからの相対パス。
inline constexpr const char* kImGuiIniFileName = "heightmap_mixer_imgui.ini";

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

    // ウィンドウを作る前に呼ぶこと。呼ばないと高 DPI 環境で
    // Windows にウィンドウごと拡大され、描画解像度が落ちてぼやける。
    static void EnableDpiAwareness();

    bool Initialize(Window& window, rhi::Device& device);
    void Shutdown();

    void BeginFrame();
    void EndFrame(ID3D12GraphicsCommandList* commandList);

    // Window のメッセージフックから呼ぶ。true ならウィンドウ側では処理しない。
    bool HandleMessage(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

    // 高 DPI 環境でのスケール。既定のウィンドウ位置・サイズにも掛けること。
    float DpiScale() const { return m_dpiScale; }

private:
    void LoadFonts(float dpiScale);

    rhi::Device* m_device = nullptr;
    float m_dpiScale = 1.0f;
    bool m_initialized = false;
};

}  // namespace hm
