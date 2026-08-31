#include "app/Application.h"

#include <Windows.h>

// --- DirectX 12 Agility SDK ------------------------------------------------
// 実行ファイルからエクスポートすることで、D3D12/ 配下の新しいランタイムが使われる。
extern "C" {
__declspec(dllexport) extern const UINT D3D12SDKVersion = 619;
__declspec(dllexport) extern const char* D3D12SDKPath = ".\\D3D12\\";
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    hm::Application app;
    if (!app.Initialize()) {
        app.Shutdown();
        return 1;
    }

    const int result = app.Run();
    app.Shutdown();
    return result;
}
