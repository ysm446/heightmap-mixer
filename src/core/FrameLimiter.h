#pragma once

#include <Windows.h>

#include <chrono>

namespace mm {

// フレームレートの上限。1 フレームの終わりに Wait() を呼ぶ。
//
// **Sleep ではなく高分解能の待機タイマーを使う。** 既定の Sleep は 15ms 刻みで、
// 60fps（16.7ms）を狙うと 1 フレームおきに 30fps へ落ちる。
// タイマーの分解能をプロセス全体で変える（timeBeginPeriod）方法は他のアプリにも
// 影響するので採らない。
class FrameLimiter {
public:
    FrameLimiter() = default;
    ~FrameLimiter();

    FrameLimiter(const FrameLimiter&) = delete;
    FrameLimiter& operator=(const FrameLimiter&) = delete;

    // fps が 0 以下なら何もしない（上限なし）。
    //
    // wakeOnInput が真なら、入力が来た時点で待ちを打ち切る。
    // **非アクティブ時の低い上限で使う。** 10fps で素直に眠ると、クリックしてから
    // 反応するまで最大 100ms かかり、掴んだ感じが鈍る。
    void Wait(int fps, bool wakeOnInput);

private:
    // 次のフレームを始めてよい時刻。**前回の狙った時刻から積む**ので、
    // 1 フレームだけ長引いても平均のレートがずれない。
    std::chrono::steady_clock::time_point m_next{};
    HANDLE m_timer = nullptr;
    bool m_timerChecked = false;
};

}  // namespace mm
