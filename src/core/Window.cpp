#include "core/Window.h"

#include "core/Log.h"

namespace hm {
namespace {

constexpr const wchar_t* kWindowClassName = L"HeightmapMixerWindowClass";

}  // namespace

Window::~Window() {
    Destroy();
}

bool Window::Create(const wchar_t* title, uint32_t width, uint32_t height) {
    const HINSTANCE instance = ::GetModuleHandleW(nullptr);

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = &Window::WndProcThunk;
    wc.hInstance = instance;
    wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kWindowClassName;
    if (::RegisterClassExW(&wc) == 0 && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        HM_LOG_ERROR("RegisterClassExW に失敗しました (0x%08lX)", ::GetLastError());
        return false;
    }

    RECT rect = {0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    ::AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);

    m_hwnd = ::CreateWindowExW(0, kWindowClassName, title, WS_OVERLAPPEDWINDOW,
                               CW_USEDEFAULT, CW_USEDEFAULT,
                               rect.right - rect.left, rect.bottom - rect.top,
                               nullptr, nullptr, instance, this);
    if (m_hwnd == nullptr) {
        HM_LOG_ERROR("CreateWindowExW に失敗しました (0x%08lX)", ::GetLastError());
        return false;
    }

    // 実際に載ったモニタの作業領域に収める。
    // SPI_GETWORKAREA はプライマリモニタしか見ないため、ここで改めて調整する。
    MONITORINFO monitorInfo = {};
    monitorInfo.cbSize = sizeof(monitorInfo);
    if (::GetMonitorInfoW(::MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST), &monitorInfo)) {
        const LONG workWidth = monitorInfo.rcWork.right - monitorInfo.rcWork.left;
        const LONG workHeight = monitorInfo.rcWork.bottom - monitorInfo.rcWork.top;

        RECT windowRect = {};
        ::GetWindowRect(m_hwnd, &windowRect);
        const LONG windowWidth = windowRect.right - windowRect.left;
        const LONG windowHeight = windowRect.bottom - windowRect.top;

        if (windowWidth > workWidth || windowHeight > workHeight) {
            const LONG fittedWidth = (windowWidth < workWidth) ? windowWidth : workWidth;
            const LONG fittedHeight = (windowHeight < workHeight) ? windowHeight : workHeight;
            ::SetWindowPos(m_hwnd, nullptr, monitorInfo.rcWork.left, monitorInfo.rcWork.top,
                           fittedWidth, fittedHeight, SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }

    RECT client = {};
    ::GetClientRect(m_hwnd, &client);
    m_width = static_cast<uint32_t>(client.right - client.left);
    m_height = static_cast<uint32_t>(client.bottom - client.top);

    ::ShowWindow(m_hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(m_hwnd);
    return true;
}

void Window::Destroy() {
    if (m_hwnd != nullptr) {
        ::DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
}

bool Window::PumpMessages() {
    MSG msg = {};
    while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
        if (msg.message == WM_QUIT) {
            m_shouldClose = true;
        }
    }
    return !m_shouldClose;
}

LRESULT CALLBACK Window::WndProcThunk(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    Window* self = nullptr;
    if (msg == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        self = static_cast<Window*>(create->lpCreateParams);
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->m_hwnd = hwnd;
    } else {
        self = reinterpret_cast<Window*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (self != nullptr) {
        return self->WndProc(hwnd, msg, wparam, lparam);
    }
    return ::DefWindowProcW(hwnd, msg, wparam, lparam);
}

LRESULT Window::WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (m_messageHook && m_messageHook(hwnd, msg, wparam, lparam)) {
        return 1;
    }

    switch (msg) {
        case WM_SIZE: {
            m_minimized = (wparam == SIZE_MINIMIZED);
            const auto width = static_cast<uint32_t>(LOWORD(lparam));
            const auto height = static_cast<uint32_t>(HIWORD(lparam));
            if (!m_minimized && width > 0 && height > 0 &&
                (width != m_width || height != m_height)) {
                m_width = width;
                m_height = height;
                if (m_resizeCallback) {
                    m_resizeCallback(m_width, m_height);
                }
            }
            return 0;
        }
        case WM_SYSCOMMAND:
            // Alt キー単独でのシステムメニュー表示を抑止する。
            if ((wparam & 0xFFF0) == SC_KEYMENU) {
                return 0;
            }
            break;
        case WM_CLOSE:
            m_shouldClose = true;
            return 0;
        case WM_DESTROY:
            ::PostQuitMessage(0);
            return 0;
        default:
            break;
    }
    return ::DefWindowProcW(hwnd, msg, wparam, lparam);
}

}  // namespace hm
