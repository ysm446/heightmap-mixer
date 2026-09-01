// フレームレート上限のテスト。
//
// **時間の話なのでスクリーンショットには写らない。** 上限どおりに待つこと、
// 上限なしでは待たないこと、長く止まった後に取り返そうとしないことを見る。

#include "TestSupport.h"

#include "core/FrameLimiter.h"

#include <chrono>
#include <thread>

namespace {

using mm::tests::Check;
using mm::tests::Section;
using Clock = std::chrono::steady_clock;

// fps の上限で frames 回まわしたときの経過ミリ秒。
long long ElapsedMs(int fps, int frames) {
    mm::FrameLimiter limiter;
    const Clock::time_point start = Clock::now();
    for (int i = 0; i < frames; ++i) {
        limiter.Wait(fps, false);
    }
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
}

}  // namespace

void RunFrameLimiterTests() {
    Section("フレームレート上限");

    // 20fps で 5 フレーム = 250ms。待機タイマーの精度と OS のスケジューリングを
    // 見込んで幅を持たせる。**下限は必ず見る**（待っていなければ意味がない）。
    const long long limited = ElapsedMs(20, 5);
    Check(limited >= 200, "上限どおりに待つ（20fps x 5 フレームで 200ms 以上）");
    Check(limited < 500, "待ちすぎない（同 500ms 未満）");

    // 0 は上限なし。呼んでも素通りする。
    const long long unlimited = ElapsedMs(0, 100);
    Check(unlimited < 50, "上限なし（0）では待たない");

    // **長く止まった後に取り返そうとしない。** 前回の狙った時刻から素直に積むと、
    // 読み込みなどで数秒止まった後に何フレームも待たずに走ってしまう。
    {
        mm::FrameLimiter limiter;
        limiter.Wait(20, false);
        std::this_thread::sleep_for(std::chrono::milliseconds(300));

        const Clock::time_point start = Clock::now();
        limiter.Wait(20, false);
        limiter.Wait(20, false);
        const long long elapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
        Check(elapsed >= 80, "長く止まった後も、次のフレームからは上限どおりに待つ");
    }
}
