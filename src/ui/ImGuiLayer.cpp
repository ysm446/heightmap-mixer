#include "ui/ImGuiLayer.h"

#include "core/Log.h"
#include "core/Window.h"
#include "rhi/Device.h"

#include <imgui.h>
#include <imgui_impl_dx12.h>
#include <imgui_impl_win32.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wparam,
                                                             LPARAM lparam);

namespace hm {
namespace {

// ImGui バックエンドからのディスクリプタ確保要求を、こちらのアロケータへ橋渡しする。
void SrvDescriptorAlloc(ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE* outCpu,
                        D3D12_GPU_DESCRIPTOR_HANDLE* outGpu) {
    auto* device = static_cast<rhi::Device*>(info->UserData);
    const rhi::DescriptorHandle handle = device->SrvHeap().Allocate();
    *outCpu = handle.cpu;
    *outGpu = handle.gpu;
}

void SrvDescriptorFree(ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE cpu,
                       D3D12_GPU_DESCRIPTOR_HANDLE gpu) {
    (void)gpu;
    auto* device = static_cast<rhi::Device*>(info->UserData);
    rhi::DescriptorHeap& heap = device->SrvHeap();
    const D3D12_CPU_DESCRIPTOR_HANDLE start = heap.At(0).cpu;
    const uint32_t index =
        static_cast<uint32_t>((cpu.ptr - start.ptr) / heap.DescriptorSize());
    heap.Free(heap.At(index));
}

}  // namespace

ImGuiLayer::~ImGuiLayer() {
    Shutdown();
}

bool ImGuiLayer::Initialize(Window& window, rhi::Device& device) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = "heightmap_mixer_imgui.ini";

    ImGui::StyleColorsDark();

    if (!ImGui_ImplWin32_Init(window.Handle())) {
        HM_LOG_ERROR("ImGui_ImplWin32_Init に失敗しました");
        return false;
    }

    ImGui_ImplDX12_InitInfo info = {};
    info.Device = device.GetDevice();
    info.CommandQueue = device.GetCommandQueue();
    info.NumFramesInFlight = static_cast<int>(rhi::kFrameCount);
    info.RTVFormat = rhi::kBackBufferFormat;
    info.DSVFormat = DXGI_FORMAT_UNKNOWN;
    info.UserData = &device;
    info.SrvDescriptorHeap = device.SrvHeap().Heap();
    info.SrvDescriptorAllocFn = &SrvDescriptorAlloc;
    info.SrvDescriptorFreeFn = &SrvDescriptorFree;

    if (!ImGui_ImplDX12_Init(&info)) {
        HM_LOG_ERROR("ImGui_ImplDX12_Init に失敗しました");
        return false;
    }

    LoadFonts();

    m_device = &device;
    m_initialized = true;
    return true;
}

void ImGuiLayer::LoadFonts() {
    // 日本語を表示できるよう、システムフォントを優先して読み込む。
    static const char* const kCandidates[] = {
        "C:/Windows/Fonts/YuGothM.ttc",
        "C:/Windows/Fonts/meiryo.ttc",
        "C:/Windows/Fonts/msgothic.ttc",
    };

    ImGuiIO& io = ImGui::GetIO();
    for (const char* path : kCandidates) {
        if (io.Fonts->AddFontFromFileTTF(path, 16.0f) != nullptr) {
            HM_LOG_INFO("フォントを読み込みました: %s", path);
            return;
        }
    }
    HM_LOG_WARN("日本語フォントが見つかりませんでした。既定フォントを使用します");
    io.Fonts->AddFontDefault();
}

void ImGuiLayer::Shutdown() {
    if (!m_initialized) {
        return;
    }
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    m_initialized = false;
    m_device = nullptr;
}

void ImGuiLayer::BeginFrame() {
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

void ImGuiLayer::EndFrame(ID3D12GraphicsCommandList* commandList) {
    ImGui::Render();
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList);
}

bool ImGuiLayer::HandleMessage(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (!m_initialized) {
        return false;
    }
    return ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam) != 0;
}

}  // namespace hm
