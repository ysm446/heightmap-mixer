#include "io/AppSettings.h"

#include "core/Log.h"

#include <nlohmann/json.hpp>

#include <Windows.h>

#include <algorithm>
#include <fstream>
#include <string>
#include <system_error>

namespace mm::io {
namespace {

namespace fs = std::filesystem;
using nlohmann::json;

constexpr const char* kFormat = "material-mixer.settings";
constexpr int kVersion = 1;

fs::path SettingsPath() {
    return AppDataDirectory() / L"settings.json";
}

}  // namespace

fs::path AppDataDirectory() {
    const DWORD needed = ::GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
    if (needed > 0) {
        std::wstring value;
        value.resize(needed);
        const DWORD written = ::GetEnvironmentVariableW(L"LOCALAPPDATA", value.data(), needed);
        if (written > 0) {
            value.resize(written);
            return fs::path(value) / L"material-mixer";
        }
    }
    return fs::path(L".");
}

void AppSettings::Load() {
    m_ui = UiSettings{};

    std::ifstream stream(SettingsPath(), std::ios::binary);
    if (!stream.is_open()) {
        return;  // まだ設定が無いだけ。既定値で始める。
    }

    // 例外は使わない方針なので、パース失敗は discarded で受ける。
    const json document = json::parse(stream, nullptr, false);
    if (document.is_discarded() || !document.is_object()) {
        MM_LOG_WARN("設定を読めませんでした。既定値で始めます");
        return;
    }
    const auto format = document.find("format");
    if (format == document.end() || !format->is_string() ||
        format->get<std::string>() != kFormat) {
        return;
    }

    const auto ui = document.find("ui");
    if (ui == document.end() || !ui->is_object()) {
        return;
    }
    if (const auto follow = ui->find("followSystemScale");
        follow != ui->end() && follow->is_boolean()) {
        m_ui.followSystemScale = follow->get<bool>();
    }
    if (const auto scale = ui->find("manualScale"); scale != ui->end() && scale->is_number()) {
        // 壊れた値でも操作不能にならないよう、範囲へ丸める。
        m_ui.manualScale = std::clamp(scale->get<float>(), 0.5f, 4.0f);
    }
}

bool AppSettings::Save() const {
    const fs::path path = SettingsPath();
    std::error_code error;
    if (const fs::path parent = path.parent_path(); !parent.empty()) {
        fs::create_directories(parent, error);
    }

    json ui;
    ui["followSystemScale"] = m_ui.followSystemScale;
    ui["manualScale"] = m_ui.manualScale;

    json document;
    document["format"] = kFormat;
    document["version"] = kVersion;
    document["ui"] = std::move(ui);

    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream.is_open()) {
        MM_LOG_WARN("設定を保存できませんでした");
        return false;
    }
    stream << document.dump(2) << '\n';
    return stream.good();
}

}  // namespace mm::io
