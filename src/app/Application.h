#pragma once

#include "compositor/MaterialLibrary.h"
#include "compositor/MaterialStack.h"
#include "compositor/PaintMask.h"
#include "compositor/TextureLibrary.h"
#include "core/Log.h"
#include "core/Window.h"
#include "renderer/PreviewRenderer.h"
#include "rhi/Device.h"
#include "rhi/PipelineCache.h"
#include "rhi/ShaderCompiler.h"
#include "ui/ImGuiLayer.h"

#include <imgui.h>

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

namespace mm {

// コマンドラインから渡せる起動オプション。
struct StartupOptions {
    // 起動時に読み込む HDRI。空なら手続き的な空を使う。
    std::filesystem::path hdriPath;
    // 起動時にテクスチャライブラリへ読み込む画像。--texture を繰り返し指定できる。
    std::vector<std::filesystem::path> texturePaths;
    // 起動時に開くプロジェクト (.mmproj)。空なら既定のスタックで始める。
    std::filesystem::path projectPath;
    // 指定すると、数フレーム描いてからプロジェクトを保存して終了する。
    // 保存と読み込みを対話なしで確かめるための開発用オプション。
    std::filesystem::path saveProjectPath;
    // 指定すると、数フレーム描いてからビューポートを PNG に書き出して終了する。
    // 画面キャプチャに頼らず描画結果を確認するための開発用オプション。
    std::filesystem::path screenshotPath;
    // 指定すると、ウィンドウ全体（UI 込み）を PNG に書き出して終了する。
    // 画面キャプチャは他ウィンドウを掴むことがあるため、確認にはこちらを使う。
    std::filesystem::path uiScreenshotPath;
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
    // 既定のドックレイアウトを組む。ini に配置が無いときと、明示的な要求で呼ぶ。
    void BuildDefaultLayout(ImGuiID dockspaceId);
    void DrawViewportPanel();
    void DrawMaterialPanel();
    void DrawLightingPanel();
    void DrawInfoPanel();
    void DrawLayerPanel();
    void DrawMaterialLibraryPanel();
    void DrawTextureLibraryPanel();
    // ファイルメニュー。要求を積むだけで、読み書きはフレームの外で行う。
    void DrawFileMenu();
    // 画面下端のステータスバー。直近の通知と、いま何を持っているかを出す。
    // ドックスペースより前に呼ぶこと（作業領域をバーのぶん狭める）。
    void DrawStatusBar();
    // ログをステータスバーへ流す。Initialize で SetLogSink に登録する。
    void PushStatus(LogLevel level, const char* text);
    // エクスプローラから落とされたファイルを、拡張子で行き先へ振り分ける。
    void HandleDroppedFiles(const std::vector<std::filesystem::path>& paths);
    // プロジェクトとマテリアルの読み書き、テクスチャの追加と削除。
    // どれも GPU 待機を伴うため、フレームの外（Run のフレーム前）で呼ぶ。
    void ProcessPendingFileWork();
    // 中身を空にして作り直す。プロジェクトを開く前と「新規」で使う。
    void ResetProject();
    // ウィンドウタイトルを「プロジェクト名 - Material Mixer」に揃える。
    void UpdateWindowTitle();
    // このテクスチャを参照しているマテリアルとマスクの数。一覧に出す。
    size_t CountTextureUsers(compositor::TextureId id) const;
    // レイヤー一覧。ドラッグで並べ替える。
    void DrawLayerList();
    // ペイントの対象になるレイヤー。ペイントモードで、選択中のレイヤーが
    // ペイントマスクを持つときだけ返す。
    compositor::MaterialLayer* CurrentPaintLayer();
    // レイヤーパネルのマスク欄に出すペイント関連の UI。
    bool DrawPaintSection(compositor::MaterialLayer& layer);
    // ビューポート上のドラッグをブラシへ渡す。ペイントモードのときだけ呼ぶ。
    void HandlePaintInput(compositor::MaterialLayer& layer, bool itemActive,
                          const ImVec2& imageOrigin, const ImVec2& imageSize);

    Window m_window;
    rhi::Device m_device;
    rhi::ShaderCompiler m_shaderCompiler;
    rhi::PipelineCache m_pipelineCache;
    renderer::PreviewRenderer m_renderer;
    compositor::MaterialStack m_materialStack;
    compositor::TextureLibrary m_textureLibrary;
    compositor::MaterialLibrary m_materialLibrary;
    compositor::PaintMaskStore m_paintMasks;
    int m_selectedMaterial = 0;
    // ORD をまとめて割り当てるときに選ぶテクスチャ（UI の一時状態）。
    compositor::TextureId m_ordTexture = compositor::kNoTexture;
    compositor::BrushSettings m_brush;
    // ペイントモード中はビューポートの左ドラッグがブラシになる。
    bool m_paintMode = false;
    // ストローク中の状態。前フレームのカーソル位置から線分としてブラシを積む。
    bool m_strokeActive = false;
    float m_strokeLastX = 0.0f;
    float m_strokeLastY = 0.0f;
    int m_selectedLayer = 0;
    int m_selectedTexture = 0;
    // 読み込んだ直後のテクスチャを一覧に見せるための要求。
    // 一覧はスクロールするので、追加しただけでは枠外に入って気づけない。
    bool m_scrollToSelectedTexture = false;

    // ステータスバーに出す直近の通知。ログから受け取る。
    // 時刻は ImGui に依存させない（ログはコンテキストが無い時期にも来る）。
    struct StatusMessage {
        std::string text;
        LogLevel level = LogLevel::Info;
        std::chrono::steady_clock::time_point time{};
        bool valid = false;
    };
    StatusMessage m_status;
    // 読み込みは GPU 待機を伴うため、フレームの外で処理する。
    std::vector<std::filesystem::path> m_pendingTexturePaths;

    // --- ファイル操作の保留 -------------------------------------------------
    // ダイアログはフレームの中で出すが、読み書きは GPU 待機を伴うので、
    // 選ばれたパスをここへ積んでおき、次のフレームの頭で処理する。
    std::filesystem::path m_projectPath;  // 現在のプロジェクト。未保存なら空
    std::filesystem::path m_pendingProjectSave;
    std::filesystem::path m_pendingProjectOpen;
    std::filesystem::path m_pendingMaterialExport;
    std::filesystem::path m_pendingMaterialImport;
    compositor::MaterialAssetId m_pendingExportMaterial = compositor::kNoMaterialAsset;
    compositor::TextureId m_pendingTextureRemove = compositor::kNoTexture;
    bool m_pendingProjectNew = false;
    ImGuiLayer m_imgui;

    // ビューポートの表示サイズ。UI 側で決まり、次のフレーム頭で反映する。
    uint32_t m_requestedViewportWidth = 512;
    uint32_t m_requestedViewportHeight = 512;

    StartupOptions m_options;

    float m_clearColor[4] = {0.09f, 0.09f, 0.11f, 1.0f};
    bool m_vsync = true;
    bool m_showDemoWindow = false;
    // ドックレイアウトの初期化。ini に配置が無ければ既定レイアウトを組む。
    bool m_layoutChecked = false;
    bool m_rebuildLayout = false;
    bool m_hotReloadEnabled = true;
    uint32_t m_frameCounter = 0;
};

}  // namespace mm
