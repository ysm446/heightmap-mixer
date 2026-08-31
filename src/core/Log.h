#pragma once

#include <functional>

namespace mm {

enum class LogLevel {
    Info,
    Warn,
    Error,
};

// デバッガ出力とコンソールの両方へ書き出す。書式は printf 互換。
void LogMessage(LogLevel level, const char* fmt, ...);

// ログの追加の出力先。UI のステータスバーへ直近のメッセージを出すために使う。
// 設定できるのは 1 つだけで、空の関数を渡すと解除できる。
//
// **持ち主が壊れる前に必ず解除すること。** 解除しないと、破棄したオブジェクトを
// 指したままのシンクが後続のログで呼ばれる。
using LogSink = std::function<void(LogLevel, const char*)>;
void SetLogSink(LogSink sink);

// 回復不能な初期化失敗などで使う。メッセージボックスを出してプロセスを終了する。
[[noreturn]] void FatalExit(const char* fmt, ...);

}  // namespace mm

#define MM_LOG_INFO(...)  ::mm::LogMessage(::mm::LogLevel::Info, __VA_ARGS__)
#define MM_LOG_WARN(...)  ::mm::LogMessage(::mm::LogLevel::Warn, __VA_ARGS__)
#define MM_LOG_ERROR(...) ::mm::LogMessage(::mm::LogLevel::Error, __VA_ARGS__)
