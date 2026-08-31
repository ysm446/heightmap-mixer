#include "rhi/Device.h"

#include "core/Log.h"

#include <pix3.h>

#include <utility>

namespace hm::rhi {
namespace {

// ImGui のフォントアトラスや将来のテクスチャ用に、余裕をもって確保しておく。
constexpr uint32_t kSrvHeapCapacity = 1024;

// 1 フレームあたりのアップロード容量。定数バッファと小さめの転送を想定した初期値。
constexpr uint64_t kUploadBytesPerFrame = 16ull * 1024 * 1024;

}  // namespace

Device::~Device() {
    Shutdown();
}

bool Device::Initialize(HWND hwnd, uint32_t width, uint32_t height, bool enableDebugLayer) {
    m_width = width;
    m_height = height;

    if (!CreateFactoryAndDevice(enableDebugLayer)) {
        return false;
    }
    if (!CreateCommandObjects()) {
        return false;
    }
    if (!CreateSwapChain(hwnd, width, height)) {
        return false;
    }
    if (!m_rtvHeap.Create(m_device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, kFrameCount, false)) {
        return false;
    }
    if (!m_srvHeap.Create(m_device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                          kSrvHeapCapacity, true)) {
        return false;
    }
    if (!CreateBackBufferViews()) {
        return false;
    }
    if (!m_allocator.Create(m_device.Get(), m_adapter.Get(), &m_srvHeap)) {
        return false;
    }
    if (!m_uploadRing.Create(m_allocator, kUploadBytesPerFrame)) {
        return false;
    }

    m_initialized = true;
    return true;
}

void Device::Defer(ComPtr<IUnknown> object) {
    // 現在記録中のフレームは m_nextFenceValue で Signal される。
    m_deletionQueue.Push(std::move(object), m_nextFenceValue);
}

uint64_t Device::CompletedFenceValue() const {
    return m_fence ? m_fence->GetCompletedValue() : 0;
}

bool Device::CreateFactoryAndDevice(bool enableDebugLayer) {
    UINT factoryFlags = 0;

    if (enableDebugLayer) {
        ComPtr<ID3D12Debug> debug;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
            debug->EnableDebugLayer();
            HM_LOG_INFO("D3D12 デバッグレイヤーを有効化しました");

            ComPtr<ID3D12Debug1> debug1;
            if (SUCCEEDED(debug.As(&debug1))) {
                debug1->SetEnableGPUBasedValidation(TRUE);
                HM_LOG_INFO("GPU ベースバリデーションを有効化しました");
            }
            factoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
        } else {
            HM_LOG_WARN("D3D12 デバッグレイヤーを取得できませんでした（Graphics Tools 未導入の可能性）");
        }
    }

    if (!HM_CHECK_HR(CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&m_factory)))) {
        return false;
    }

    BOOL allowTearing = FALSE;
    if (SUCCEEDED(m_factory->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING,
                                                 &allowTearing, sizeof(allowTearing)))) {
        m_allowTearing = (allowTearing == TRUE);
    }

    // 高性能アダプタから順に、D3D12 デバイスを作れるものを選ぶ。
    for (UINT i = 0;; ++i) {
        ComPtr<IDXGIAdapter4> adapter;
        if (m_factory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                                  IID_PPV_ARGS(&adapter)) == DXGI_ERROR_NOT_FOUND) {
            break;
        }

        DXGI_ADAPTER_DESC3 desc = {};
        adapter->GetDesc3(&desc);
        if ((desc.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE) != 0) {
            continue;
        }

        if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0,
                                        IID_PPV_ARGS(&m_device)))) {
            m_adapter = adapter;
            HM_LOG_INFO("アダプタ: %ls (VRAM %llu MB)", desc.Description,
                        static_cast<unsigned long long>(desc.DedicatedVideoMemory / (1024 * 1024)));
            break;
        }
    }

    if (!m_device) {
        HM_LOG_ERROR("D3D12 デバイスを作成できるアダプタが見つかりませんでした");
        return false;
    }

    // シェーダモデル 6.6 を要求する（bindless / 合成シェーダの前提）。
    D3D12_FEATURE_DATA_SHADER_MODEL shaderModel = {D3D_SHADER_MODEL_6_6};
    if (FAILED(m_device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shaderModel,
                                             sizeof(shaderModel))) ||
        shaderModel.HighestShaderModel < D3D_SHADER_MODEL_6_6) {
        HM_LOG_ERROR("シェーダモデル 6.6 に対応していません");
        return false;
    }

    // bindless（ResourceDescriptorHeap）は Resource Binding Tier 3 を前提とする。
    D3D12_FEATURE_DATA_D3D12_OPTIONS options = {};
    if (FAILED(m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options,
                                             sizeof(options))) ||
        options.ResourceBindingTier < D3D12_RESOURCE_BINDING_TIER_3) {
        HM_LOG_ERROR("Resource Binding Tier 3 に対応していません（bindless に必要）");
        return false;
    }

    // デバッガ未接続で SetBreakOnSeverity を有効にすると、警告のたびにプロセスが落ちる。
    // デバッガ接続時のみ break させ、それ以外はメッセージの出力に留める。
    if (enableDebugLayer && ::IsDebuggerPresent()) {
        ComPtr<ID3D12InfoQueue> infoQueue;
        if (SUCCEEDED(m_device.As(&infoQueue))) {
            infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
            infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
            infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, TRUE);
        }
    }
    return true;
}

bool Device::CreateCommandObjects() {
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    if (!HM_CHECK_HR(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue)))) {
        return false;
    }
    m_commandQueue->SetName(L"MainDirectQueue");

    for (uint32_t i = 0; i < kFrameCount; ++i) {
        if (!HM_CHECK_HR(m_device->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocators[i])))) {
            return false;
        }
    }

    if (!HM_CHECK_HR(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                 m_commandAllocators[0].Get(), nullptr,
                                                 IID_PPV_ARGS(&m_commandList)))) {
        return false;
    }
    // 作成直後は開いた状態なので閉じておく。
    if (!HM_CHECK_HR(m_commandList->Close())) {
        return false;
    }

    if (!HM_CHECK_HR(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)))) {
        return false;
    }

    m_fenceEvent = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (m_fenceEvent == nullptr) {
        HM_LOG_ERROR("フェンス用イベントの作成に失敗しました");
        return false;
    }
    return true;
}

bool Device::CreateSwapChain(HWND hwnd, uint32_t width, uint32_t height) {
    DXGI_SWAP_CHAIN_DESC1 desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.Format = kBackBufferFormat;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = kFrameCount;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
    desc.Flags = m_allowTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0u;

    ComPtr<IDXGISwapChain1> swapChain1;
    if (!HM_CHECK_HR(m_factory->CreateSwapChainForHwnd(m_commandQueue.Get(), hwnd, &desc, nullptr,
                                                       nullptr, &swapChain1))) {
        return false;
    }
    // Alt+Enter による自動フルスクリーン切り替えは使わない。
    if (!HM_CHECK_HR(m_factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER))) {
        return false;
    }
    if (!HM_CHECK_HR(swapChain1.As(&m_swapChain))) {
        return false;
    }
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
    return true;
}

bool Device::CreateBackBufferViews() {
    for (uint32_t i = 0; i < kFrameCount; ++i) {
        if (!HM_CHECK_HR(m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_backBuffers[i])))) {
            return false;
        }
        wchar_t name[32] = {};
        ::swprintf_s(name, L"BackBuffer%u", i);
        m_backBuffers[i]->SetName(name);

        if (!m_backBufferRtvs[i].IsValid()) {
            m_backBufferRtvs[i] = m_rtvHeap.Allocate();
            if (!m_backBufferRtvs[i].IsValid()) {
                return false;
            }
        }
        m_device->CreateRenderTargetView(m_backBuffers[i].Get(), nullptr, m_backBufferRtvs[i].cpu);
    }
    return true;
}

void Device::ReleaseBackBuffers() {
    for (auto& buffer : m_backBuffers) {
        buffer.Reset();
    }
}

void Device::Resize(uint32_t width, uint32_t height) {
    if (!m_initialized || width == 0 || height == 0) {
        return;
    }
    if (width == m_width && height == m_height) {
        return;
    }

    WaitForGpu();
    ReleaseBackBuffers();

    const UINT flags = m_allowTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0u;
    if (!HM_CHECK_HR(m_swapChain->ResizeBuffers(kFrameCount, width, height, kBackBufferFormat,
                                                flags))) {
        return;
    }

    m_width = width;
    m_height = height;
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
    CreateBackBufferViews();
}

ID3D12GraphicsCommandList* Device::BeginFrame(const float clearColor[4]) {
    if (!m_initialized || m_frameOpen) {
        return nullptr;
    }

    // このフレームスロットが前回投入した処理の完了を待つ。
    // m_fenceValues[i] == 0 は未使用スロットなので待たない。
    const uint64_t pending = m_fenceValues[m_frameIndex];
    if (pending != 0 && m_fence->GetCompletedValue() < pending) {
        if (!HM_CHECK_HR(m_fence->SetEventOnCompletion(pending, m_fenceEvent))) {
            return nullptr;
        }
        ::WaitForSingleObjectEx(m_fenceEvent, INFINITE, FALSE);
    }

    // このスロットの処理は完了しているので、解放待ちを回収してリングを巻き戻す。
    m_deletionQueue.Collect(m_fence->GetCompletedValue());
    m_uploadRing.BeginFrame(m_frameIndex);

    if (!HM_CHECK_HR(m_commandAllocators[m_frameIndex]->Reset())) {
        return nullptr;
    }
    if (!HM_CHECK_HR(m_commandList->Reset(m_commandAllocators[m_frameIndex].Get(), nullptr))) {
        return nullptr;
    }

    PIXBeginEvent(m_commandList.Get(), PIX_COLOR(0, 128, 255), "Frame");

    const auto toRenderTarget = CD3DX12_RESOURCE_BARRIER::Transition(
        m_backBuffers[m_frameIndex].Get(), D3D12_RESOURCE_STATE_PRESENT,
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    m_commandList->ResourceBarrier(1, &toRenderTarget);

    const D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_backBufferRtvs[m_frameIndex].cpu;
    m_commandList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    m_commandList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);

    const auto viewport = CD3DX12_VIEWPORT(0.0f, 0.0f, static_cast<float>(m_width),
                                           static_cast<float>(m_height));
    const auto scissor = CD3DX12_RECT(0, 0, static_cast<LONG>(m_width),
                                      static_cast<LONG>(m_height));
    m_commandList->RSSetViewports(1, &viewport);
    m_commandList->RSSetScissorRects(1, &scissor);

    ID3D12DescriptorHeap* heaps[] = {m_srvHeap.Heap()};
    m_commandList->SetDescriptorHeaps(1, heaps);

    m_frameOpen = true;
    return m_commandList.Get();
}

void Device::EndFrame(bool vsync) {
    if (!m_frameOpen) {
        return;
    }

    const auto toPresent = CD3DX12_RESOURCE_BARRIER::Transition(
        m_backBuffers[m_frameIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PRESENT);
    m_commandList->ResourceBarrier(1, &toPresent);

    PIXEndEvent(m_commandList.Get());

    if (!HM_CHECK_HR(m_commandList->Close())) {
        m_frameOpen = false;
        return;
    }

    ID3D12CommandList* lists[] = {m_commandList.Get()};
    m_commandQueue->ExecuteCommandLists(1, lists);

    const UINT syncInterval = vsync ? 1u : 0u;
    const UINT presentFlags = (!vsync && m_allowTearing) ? DXGI_PRESENT_ALLOW_TEARING : 0u;
    HM_CHECK_HR(m_swapChain->Present(syncInterval, presentFlags));

    m_frameOpen = false;
    MoveToNextFrame();
}

void Device::MoveToNextFrame() {
    const uint64_t value = m_nextFenceValue++;
    if (!HM_CHECK_HR(m_commandQueue->Signal(m_fence.Get(), value))) {
        return;
    }
    m_fenceValues[m_frameIndex] = value;

    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
}

void Device::WaitForGpu() {
    if (!m_commandQueue || !m_fence || m_fenceEvent == nullptr) {
        return;
    }

    const uint64_t value = m_nextFenceValue++;
    if (!HM_CHECK_HR(m_commandQueue->Signal(m_fence.Get(), value))) {
        return;
    }
    if (m_fence->GetCompletedValue() < value) {
        if (!HM_CHECK_HR(m_fence->SetEventOnCompletion(value, m_fenceEvent))) {
            return;
        }
        ::WaitForSingleObjectEx(m_fenceEvent, INFINITE, FALSE);
    }

    // ここまでで全スロットの処理は完了している。
    for (auto& fenceValue : m_fenceValues) {
        fenceValue = 0;
    }
}

void Device::Shutdown() {
    if (m_initialized) {
        WaitForGpu();
    }
    m_initialized = false;
    m_frameOpen = false;

    m_deletionQueue.Flush();
    m_uploadRing.Destroy();
    ReleaseBackBuffers();
    m_rtvHeap.Destroy();
    m_srvHeap.Destroy();
    // アロケータは、そこから確保した全リソースを解放したあとで破棄する。
    m_allocator.Destroy();
    m_commandList.Reset();
    for (auto& allocator : m_commandAllocators) {
        allocator.Reset();
    }
    m_swapChain.Reset();
    m_fence.Reset();
    if (m_fenceEvent != nullptr) {
        ::CloseHandle(m_fenceEvent);
        m_fenceEvent = nullptr;
    }
    m_commandQueue.Reset();
    m_device.Reset();
    m_adapter.Reset();
    m_factory.Reset();
}

}  // namespace hm::rhi
