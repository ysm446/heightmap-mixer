#pragma once

#include <filesystem>

namespace mm::io {

// アプリ側の状態を置くフォルダ（`%LOCALAPPDATA%/material-mixer`）。
// プロジェクトの中身ではないもの（設定、最近使ったファイル）はここへ置く。
// 環境変数が引けないときは作業ディレクトリを返す。
std::filesystem::path AppDataDirectory();

// UI の見た目に関する設定。プロジェクトではなくアプリに紐づく。
struct UiSettings {
    // Windows の表示スケール（DPI）に合わせて UI を拡大するか。
    //
    // 既定は**合わせない**。クライアント領域を実ピクセルで固定しているので、
    // 追従させると作業面積がモニタ設定によって変わる（design-guide.md の「寸法と DPI」）。
    // 高 DPI で UI が小さすぎるときに、使う人が選べるようにしてある。
    bool followSystemScale = false;
    // 追従しないときの拡大率。
    float manualScale = 1.0f;
};

// 設定ファイル（`settings.json`）の読み書き。
//
// 値を変えたら Save() を呼ぶ。書けなくても動作は続ける（次回に持ち越せないだけ）。
class AppSettings {
public:
    void Load();
    bool Save() const;

    UiSettings& Ui() { return m_ui; }
    const UiSettings& Ui() const { return m_ui; }

private:
    UiSettings m_ui;
};

}  // namespace mm::io
