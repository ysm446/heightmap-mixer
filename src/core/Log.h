#pragma once

namespace mm {

enum class LogLevel {
    Info,
    Warn,
    Error,
};

// デバッガ出力とコンソールの両方へ書き出す。書式は printf 互換。
void LogMessage(LogLevel level, const char* fmt, ...);

// 回復不能な初期化失敗などで使う。メッセージボックスを出してプロセスを終了する。
[[noreturn]] void FatalExit(const char* fmt, ...);

}  // namespace mm

#define MM_LOG_INFO(...)  ::mm::LogMessage(::mm::LogLevel::Info, __VA_ARGS__)
#define MM_LOG_WARN(...)  ::mm::LogMessage(::mm::LogLevel::Warn, __VA_ARGS__)
#define MM_LOG_ERROR(...) ::mm::LogMessage(::mm::LogLevel::Error, __VA_ARGS__)
