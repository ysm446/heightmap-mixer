#include "app/Application.h"

#include "core/Log.h"

#include <imgui.h>
#include <pix3.h>

#include <filesystem>
#include <string>

namespace hm {
namespace {

constexpr uint32_t kInitialWidth = 1600;
constexpr uint32_t kInitialHeight = 900;

// ホットリロードの走査間隔（フレーム数）。毎フレーム走査するほどの頻度は要らない。
constexpr uint32_t kHotReloadIntervalFrames = 30;

constexpr DXGI_FORMAT kPreviewFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;

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

}  // namespace

bool Application::Initialize() {
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
    if (!CreatePreviewTexture(m_previewSize)) {
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
    ReleasePreviewTexture();
    m_imgui.Shutdown();
    m_pipelineCache.Destroy();
    m_shaderCompiler.Destroy();
    m_device.Shutdown();
    m_window.Destroy();
}

bool Application::CreatePreviewTexture(uint32_t size) {
    rhi::TextureDesc desc;
    desc.width = size;
    desc.height = size;
    desc.format = kPreviewFormat;
    desc.allowUnorderedAccess = true;
    desc.createSrv = true;
    desc.initialState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    desc.debugName = L"PreviewTexture";

    if (!m_device.Allocator().CreateTexture2D(desc, m_previewTexture)) {
        HM_LOG_ERROR("プレビューテクスチャの作成に失敗しました (%u x %u)", size, size);
        return false;
    }
    m_previewSize = size;
    m_requestedPreviewSize = size;
    return true;
}

void Application::ReleasePreviewTexture() {
    if (!m_previewTexture.IsValid()) {
        return;
    }
    // ディスクリプタを返してから、リソース本体は削除キュー経由で解放する。
    m_device.Allocator().ReleaseDescriptors(m_previewTexture);
    m_device.Defer(m_previewTexture.resource);
    m_device.Defer(m_previewTexture.allocation);
    m_previewTexture = rhi::GpuTexture{};
}

void Application::DispatchPreview(ID3D12GraphicsCommandList* commandList) {
    if (!m_previewTexture.IsValid()) {
        return;
    }

    ID3D12PipelineState* pipeline = m_pipelineCache.GetCompute(L"SmokeTest.hlsl", L"CsMain");
    if (pipeline == nullptr) {
        return;
    }

    PIXBeginEvent(commandList, PIX_COLOR(255, 160, 0), "PreviewCompute");

    if (m_previewTexture.state != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
        const auto toUav = CD3DX12_RESOURCE_BARRIER::Transition(
            m_previewTexture.resource.Get(), m_previewTexture.state,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        commandList->ResourceBarrier(1, &toUav);
        m_previewTexture.state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }

    struct SmokeTestConstants {
        uint32_t outputIndex;
        uint32_t width;
        uint32_t height;
        float time;
    };

    const SmokeTestConstants constants{m_previewTexture.UavIndex(), m_previewTexture.width,
                                       m_previewTexture.height, m_time};

    commandList->SetComputeRootSignature(m_pipelineCache.GlobalRootSignature());
    commandList->SetPipelineState(pipeline);
    commandList->SetComputeRoot32BitConstants(0, sizeof(constants) / sizeof(uint32_t), &constants,
                                              0);

    constexpr uint32_t kGroupSize = 8;
    commandList->Dispatch((m_previewTexture.width + kGroupSize - 1) / kGroupSize,
                          (m_previewTexture.height + kGroupSize - 1) / kGroupSize, 1);

    // ImGui から SRV として読むため、ピクセルシェーダ可視の状態へ移す。
    const auto toSrv = CD3DX12_RESOURCE_BARRIER::Transition(
        m_previewTexture.resource.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    commandList->ResourceBarrier(1, &toSrv);
    m_previewTexture.state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

    PIXEndEvent(commandList);
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

        if (m_requestedPreviewSize != m_previewSize) {
            m_device.WaitForGpu();
            ReleasePreviewTexture();
            if (!CreatePreviewTexture(m_requestedPreviewSize)) {
                break;
            }
        }

        m_imgui.BeginFrame();
        DrawUi();

        ID3D12GraphicsCommandList* commandList = m_device.BeginFrame(m_clearColor);
        if (commandList == nullptr) {
            // フレームを開始できなかった場合は ImGui の状態を捨てて次へ進む。
            ImGui::EndFrame();
            continue;
        }

        if (m_animatePreview) {
            m_time += ImGui::GetIO().DeltaTime;
        }
        DispatchPreview(commandList);

        m_imgui.EndFrame(commandList);
        m_device.EndFrame(m_vsync);
        ++m_frameCounter;
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

    ImGui::SetNextWindowSize(ImVec2(360.0f, 400.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(24.0f, 48.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("情報")) {
        const ImGuiIO& io = ImGui::GetIO();
        ImGui::Text("heightmap-mixer %s", HM_APP_VERSION);
        ImGui::Separator();
        ImGui::Text("%.1f FPS (%.3f ms/frame)", io.Framerate, 1000.0f / io.Framerate);
        ImGui::Text("バックバッファ: %u x %u", m_device.Width(), m_device.Height());
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

    ImGui::SetNextWindowSize(ImVec2(620.0f, 700.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(408.0f, 48.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("プレビュー")) {
        static const char* const kSizeLabels[] = {"256", "512", "1024", "2048"};
        static const uint32_t kSizeValues[] = {256, 512, 1024, 2048};

        int selected = 1;
        for (int i = 0; i < IM_ARRAYSIZE(kSizeValues); ++i) {
            if (kSizeValues[i] == m_requestedPreviewSize) {
                selected = i;
                break;
            }
        }

        ImGui::Checkbox("アニメーション", &m_animatePreview);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::Combo("解像度", &selected, kSizeLabels, IM_ARRAYSIZE(kSizeLabels))) {
            m_requestedPreviewSize = kSizeValues[selected];
        }
        ImGui::Text("bindless index — UAV: %u / SRV: %u", m_previewTexture.UavIndex(),
                    m_previewTexture.SrvIndex());

        if (m_previewTexture.IsValid()) {
            const ImVec2 available = ImGui::GetContentRegionAvail();
            const float side = (available.x < available.y) ? available.x : available.y;
            if (side > 16.0f) {
                ImGui::Image(static_cast<ImTextureID>(m_previewTexture.srv.gpu.ptr),
                             ImVec2(side, side));
            }
        }
    }
    ImGui::End();

    if (m_showDemoWindow) {
        ImGui::ShowDemoWindow(&m_showDemoWindow);
    }
}

}  // namespace hm
