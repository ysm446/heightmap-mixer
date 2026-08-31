#pragma once

namespace hm {

enum class LogLevel {
    Info,
    Warn,
    Error,
};

// デバッガ出力とコンソールの両方へ書き出す。書式は printf 互換。
void LogMessage(LogLevel level, const char* fmt, ...);

// 回復不能な初期化失敗などで使う。メッセージボックスを出してプロセスを終了する。
[[noreturn]] void FatalExit(const char* fmt, ...);

}  // namespace hm

#define HM_LOG_INFO(...)  ::hm::LogMessage(::hm::LogLevel::Info, __VA_ARGS__)
#define HM_LOG_WARN(...)  ::hm::LogMessage(::hm::LogLevel::Warn, __VA_ARGS__)
#define HM_LOG_ERROR(...) ::hm::LogMessage(::hm::LogLevel::Error, __VA_ARGS__)
