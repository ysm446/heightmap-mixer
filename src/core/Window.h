#pragma once

#include <Windows.h>

#include <cstdint>
#include <functional>

namespace hm {

// Win32 ウィンドウ。メッセージフックを差し込めるようにして、UI 層への依存を持たない。
class Window {
public:
    // 追加のメッセージ処理。true を返すとそのメッセージはウィンドウ側で処理しない。
    using MessageHook = std::function<bool(HWND, UINT, WPARAM, LPARAM)>;
    using ResizeCallback = std::function<void(uint32_t, uint32_t)>;

    Window() = default;
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    // width / height はクライアント領域（描画される中身）のサイズ。
    // ウィンドウ枠のぶんは内部で足す。
    bool Create(const wchar_t* title, uint32_t width, uint32_t height);
    void Destroy();

    // メニューなどからアプリを閉じる。
    void RequestClose() { m_shouldClose = true; }

    // 溜まっているメッセージを処理する。終了要求が来ていたら false を返す。
    bool PumpMessages();

    void SetMessageHook(MessageHook hook) { m_messageHook = std::move(hook); }
    void SetResizeCallback(ResizeCallback callback) { m_resizeCallback = std::move(callback); }

    HWND Handle() const { return m_hwnd; }
    uint32_t Width() const { return m_width; }
    uint32_t Height() const { return m_height; }
    bool IsMinimized() const { return m_minimized; }

private:
    static LRESULT CALLBACK WndProcThunk(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
    LRESULT WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

    HWND m_hwnd = nullptr;
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    bool m_shouldClose = false;
    bool m_minimized = false;
    MessageHook m_messageHook;
    ResizeCallback m_resizeCallback;
};

}  // namespace hm
