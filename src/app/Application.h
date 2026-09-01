#pragma once

#include "compositor/MaterialLibrary.h"
#include "compositor/MaterialStack.h"
#include "compositor/PaintMask.h"
#include "compositor/TextureLibrary.h"
#include "core/Log.h"
#include "core/Window.h"
#include "app/UndoHistory.h"
#include "io/AppSettings.h"
#include "io/RecentFiles.h"
#include "renderer/PreviewRenderer.h"
#include "rhi/Device.h"
#include "rhi/PipelineCache.h"
#include "rhi/ShaderCompiler.h"
#include "ui/ImGuiLayer.h"
#include "ui/Toast.h"

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
    // F12 で撮ったスクリーンショットの書き出しを要求する。撮れたら通知を出す。
    void RequestScreenshot();
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
    // アプリの設定ウィンドウ（表示 > 設定）。プロジェクトに保存しない設定を置く。
    void DrawSettingsWindow();
    // 設定から決まる UI の拡大率。追従なら Windows の表示スケール。
    float DesiredUiScale() const;
    // 拡大率を掛けた既定のクライアント領域。1920x1080 を拡大率倍したもの。
    // 追従を入れたときに作業面積（論理サイズ）が変わらないようにするため。
    uint32_t DefaultClientWidth() const;
    uint32_t DefaultClientHeight() const;
    // 設定に合わせて拡大率とウィンドウの大きさを反映する。フレームの外で呼ぶこと。
    void ApplyUiScale();
    // ファイルメニュー。要求を積むだけで、読み書きはフレームの外で行う。
    void DrawFileMenu();
    // キーボードショートカット（Ctrl+N / O / S / Shift+S）。メニューと同じ入口を通す。
    void HandleShortcuts();
    void RequestOpenProject();
    // 「最近使ったプロジェクト」。開く要求を積むだけ。
    void DrawRecentMenu();
    // saveAs が偽でも、まだ保存先が決まっていなければダイアログを出す。
    void RequestSaveProject(bool saveAs);
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
    // このテクスチャを使っている場所の一覧（削除の確認に出す）。
    std::vector<std::string> CollectTextureUsers(compositor::TextureId id) const;
    // 参照している箇所の数だけを数える。毎フレーム呼ぶので文字列は作らない。
    size_t CountTextureUsers(compositor::TextureId id) const;
    // 参照が残っているテクスチャを消そうとしたときの確認。
    void DrawTextureRemoveModal();
    // レイヤー一覧。ドラッグで並べ替える。
    void DrawLayerList();
    // レイヤーを 1 枚消す。ツールバーのボタンと一覧の削除アイコンの共通の入口。
    void RemoveLayer(int index);

    // --- アンドゥ -----------------------------------------------------------
    // 対象はレイヤーとマテリアル。テクスチャの読み込みと削除、ペイントの筆致、
    // プレビュー設定は含めない（前者 2 つは GPU リソースそのもの、
    // ペイントは PaintMaskStore が別の履歴を持つ）。
    //
    // いまの文書を写し取る。
    DocumentSnapshot CaptureDocument() const;
    // 写し取った文書を書き戻す。**マテリアルの破棄を伴うのでフレームの外で呼ぶ。**
    void ApplyDocument(const DocumentSnapshot& snapshot);
    // レイヤーかマテリアルを変えたときに呼ぶ。フレームの終わりに 1 段積まれる。
    void MarkDocumentChanged();
    // 文書からも履歴からも参照されなくなったペイントマスクを破棄する。
    // レイヤーを消してもすぐには捨てないため、ここで回収する。
    void SweepPaintMasks();
    // 存在しないテクスチャ ID を「なし」に落とす。
    // テクスチャは履歴の外で消えるため、書き戻した参照が宙に浮くことがある。
    compositor::TextureId ValidTexture(compositor::TextureId id) const;
    // ペイントの対象になるレイヤー。ペイントモードで、選択中のレイヤーが
    // ペイントマスクを持つときだけ返す。
    compositor::MaterialLayer* CurrentPaintLayer();
    // レイヤーパネルのマスク欄に出すペイント関連の UI。
    bool DrawPaintSection(compositor::MaterialLayer& layer);
    // ビューポートに重ねる操作（表示モードの切り替え）。画像の描画より後に呼ぶ。
    void DrawViewportOverlay(const ImVec2& viewportMin);
    // ビューポート上の L + 左ドラッグでライトの向きを変える。
    // 掴んでいる間は true を返す（軌道やブラシへ渡さない）。
    bool HandleLightDrag(bool itemActive);
    // ビューポート上の F / A キーで視点をメッシュへ戻す。
    void HandleCameraShortcuts(bool itemHovered);
    // ライトの向きを示すギズモ。動かしている間と、その直後だけ出す。
    void DrawLightGizmo(const ImVec2& viewportMin, const ImVec2& viewportMax);
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
    // ライトの向きを掴んでいる間。ギズモは離してからも少しの間だけ残す。
    bool m_lightDragActive = false;
    double m_lightGizmoUntil = 0.0;
    // ストローク中の状態。前フレームのカーソル位置から線分としてブラシを積む。
    bool m_strokeActive = false;
    float m_strokeLastX = 0.0f;
    float m_strokeLastY = 0.0f;
    int m_selectedLayer = 0;
    int m_selectedTexture = 0;
    // 拡大プレビューで出すチャンネル。0 = RGB、1..4 = R / G / B / A。
    // ORD のように 1 枚へ複数のマップを詰めたテクスチャの中身を確かめるためのもの。
    int m_previewChannel = 0;
    // 読み込んだ直後のテクスチャを一覧に見せるための要求。
    // 一覧はスクロールするので、追加しただけでは枠外に入って気づけない。
    bool m_scrollToSelectedTexture = false;
    // 追加・複製した直後のマテリアルを一覧の枠内へ送る要求。上と同じ理由。
    bool m_scrollToSelectedMaterial = false;

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
    io::RecentFiles m_recentProjects;
    io::AppSettings m_settings;
    // 設定ウィンドウを出しているか。ドックへは収めない補助ウィンドウ。
    bool m_showSettings = false;
    std::filesystem::path m_pendingProjectSave;
    std::filesystem::path m_pendingProjectOpen;
    std::filesystem::path m_pendingMaterialExport;
    std::filesystem::path m_pendingMaterialImport;
    compositor::MaterialAssetId m_pendingExportMaterial = compositor::kNoMaterialAsset;
    compositor::TextureId m_pendingTextureRemove = compositor::kNoTexture;
    // 削除要求のあったマテリアル。一覧の描画中に消すと、描画側が erase 済みの
    // 要素を読んでしまうため、フレームの外で処理する。
    compositor::MaterialAssetId m_pendingMaterialRemove = compositor::kNoMaterialAsset;
    // 確認待ちのテクスチャ。参照が残っているときだけ入る。
    compositor::TextureId m_textureRemoveCandidate = compositor::kNoTexture;
    std::vector<std::string> m_textureRemoveUsers;
    bool m_pendingProjectNew = false;

    // --- アンドゥの状態 -----------------------------------------------------
    UndoHistory m_undoHistory;
    // 直近に確定した文書。変更を見つけたとき、これを「変更前」として積む。
    DocumentSnapshot m_committed;
    // このフレームでレイヤーかマテリアルが変わったか。フレームの終わりに畳む。
    bool m_documentDirty = false;
    // -1 でアンドゥ、+1 でリドゥ。マテリアルの破棄を伴うのでフレームの外で処理する。
    int m_pendingHistoryStep = 0;
    // 参照が切れたペイントマスクの回収を予約する。破棄は GPU 待機を伴う。
    bool m_pendingPaintSweep = false;

    ImGuiLayer m_imgui;
    // 右下に出す通知。保存の完了などを知らせる。
    ui::ToastQueue m_toasts;
    // F12 が押されたフレームに立つ。EndFrame で撮ってから下ろす。
    bool m_screenshotPending = false;

    // ビューポートの表示サイズ。UI 側で決まり、次のフレーム頭で反映する。
    uint32_t m_requestedViewportWidth = 512;
    uint32_t m_requestedViewportHeight = 512;

    StartupOptions m_options;

    float m_clearColor[4] = {0.09f, 0.09f, 0.11f, 1.0f};
    bool m_vsync = true;
    // CoInitializeEx が成功したときだけ CoUninitialize する。
    bool m_comInitialized = false;
    bool m_showDemoWindow = false;
    // ドックレイアウトの初期化。ini に配置が無ければ既定レイアウトを組む。
    bool m_layoutChecked = false;
    bool m_rebuildLayout = false;
    // 既定レイアウトを組んだ直後に、前面へ出したいタブを押さえるための残りフレーム数。
    // ini に無いウィンドウは作られた順で前面が決まってしまうため、明示的に上書きする。
    int m_focusDefaultTabs = 0;
    bool m_hotReloadEnabled = true;
    uint32_t m_frameCounter = 0;
};

}  // namespace mm
