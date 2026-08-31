#pragma once

#include "core/Window.h"
#include "renderer/PreviewRenderer.h"
#include "rhi/Device.h"
#include "rhi/PipelineCache.h"
#include "rhi/ShaderCompiler.h"
#include "ui/ImGuiLayer.h"

#include <filesystem>

namespace hm {

// コマンドラインから渡せる起動オプション。
struct StartupOptions {
    // 起動時に読み込む HDRI。空なら手続き的な空を使う。
    std::filesystem::path hdriPath;
    // 指定すると、数フレーム描いてからビューポートを PNG に書き出して終了する。
    // 画面キャプチャに頼らず描画結果を確認するための開発用オプション。
    std::filesystem::path screenshotPath;
    uint32_t screenshotFrame = 8;
};

// アプリ本体。ウィンドウ、デバイス、UI の生存期間とフレームループを持つ。
class Application {
public:
    bool Initialize(const StartupOptions& options);
    void Shutdown();
    int Run();

private:
    void PollShaderHotReload();
    void DrawUi();
    void DrawViewportPanel(bool applyLayout);
    void DrawMaterialPanel(bool applyLayout);
    void DrawLightingPanel(bool applyLayout);
    void DrawInfoPanel(bool applyLayout);

    Window m_window;
    rhi::Device m_device;
    rhi::ShaderCompiler m_shaderCompiler;
    rhi::PipelineCache m_pipelineCache;
    renderer::PreviewRenderer m_renderer;
    ImGuiLayer m_imgui;

    // ビューポートの表示サイズ。UI 側で決まり、次のフレーム頭で反映する。
    uint32_t m_requestedViewportWidth = 512;
    uint32_t m_requestedViewportHeight = 512;

    // HDRI の読み込み先パス（UI 入力用）。
    char m_hdrPathBuffer[512] = {};

    StartupOptions m_options;

    float m_clearColor[4] = {0.09f, 0.09f, 0.11f, 1.0f};
    bool m_vsync = true;
    bool m_showDemoWindow = false;
    bool m_hotReloadEnabled = true;
    // 保存済みレイアウト(ini)が無い初回起動のときだけ既定配置を適用する。
    bool m_applyDefaultLayout = false;
    uint32_t m_frameCounter = 0;
};

}  // namespace hm
