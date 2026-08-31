#pragma once

#include "core/Window.h"
#include "rhi/Device.h"
#include "rhi/PipelineCache.h"
#include "rhi/ShaderCompiler.h"
#include "ui/ImGuiLayer.h"

namespace hm {

// アプリ本体。ウィンドウ、デバイス、UI の生存期間とフレームループを持つ。
class Application {
public:
    bool Initialize();
    void Shutdown();
    int Run();

private:
    bool CreatePreviewTexture(uint32_t size);
    void ReleasePreviewTexture();
    void DispatchPreview(ID3D12GraphicsCommandList* commandList);
    void PollShaderHotReload();
    void DrawUi();

    Window m_window;
    rhi::Device m_device;
    rhi::ShaderCompiler m_shaderCompiler;
    rhi::PipelineCache m_pipelineCache;
    ImGuiLayer m_imgui;

    // M1 の疎通確認用。DXC / PSO キャッシュ / bindless / 状態遷移を一通り通す。
    rhi::GpuTexture m_previewTexture;
    uint32_t m_previewSize = 512;
    uint32_t m_requestedPreviewSize = 512;
    float m_time = 0.0f;
    bool m_animatePreview = true;

    float m_clearColor[4] = {0.09f, 0.09f, 0.11f, 1.0f};
    bool m_vsync = true;
    bool m_showDemoWindow = false;
    bool m_hotReloadEnabled = true;
    uint32_t m_frameCounter = 0;
};

}  // namespace hm
