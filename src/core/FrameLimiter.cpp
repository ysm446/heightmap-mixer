#include "core/FrameLimiter.h"

#include <thread>

namespace mm {
namespace {

using Clock = std::chrono::steady_clock;

// 待機タイマーを作る。高分解能版が使えない環境では通常のものへ落とす。
HANDLE CreateTimer() {
#if defined(CREATE_WAITABLE_TIMER_HIGH_RESOLUTION)
    if (HANDLE timer = ::CreateWaitableTimerExW(nullptr, nullptr,
                                                CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                                TIMER_ALL_ACCESS);
        timer != nullptr) {
        return timer;
    }
#endif
    return ::CreateWaitableTimerW(nullptr, FALSE, nullptr);
}

}  // namespace

FrameLimiter::~FrameLimiter() {
    if (m_timer != nullptr) {
        ::CloseHandle(m_timer);
        m_timer = nullptr;
    }
}

void FrameLimiter::Wait(int fps, bool wakeOnInput) {
    if (fps <= 0) {
        // 上限なしへ戻した直後に、溜まった遅れを取り戻そうとして走らないよう、
        // 積み上げていた時刻は捨てる。
        m_next = Clock::time_point{};
        return;
    }

    const auto period = std::chrono::nanoseconds(1'000'000'000LL / fps);
    const Clock::time_point now = Clock::now();

    // 初回、上限を変えた直後、読み込みなどで大きく止まった後は、いまから積み直す。
    // そうしないと「遅れを取り戻す」ために何フレームも待たずに走ってしまう。
    if (m_next == Clock::time_point{} || m_next + period < now) {
        m_next = now + period;
    } else {
        m_next += period;
    }

    const auto remaining = m_next - now;
    if (remaining <= std::chrono::nanoseconds::zero()) {
        return;
    }

    if (wakeOnInput) {
        // 入力が来たら打ち切る。**戻り値は見ない**（起きた理由に関わらず、
        // 次のフレームでメッセージを汲めばよい）。
        const auto milliseconds =
            std::chrono::duration_cast<std::chrono::milliseconds>(remaining).count();
        ::MsgWaitForMultipleObjectsEx(0, nullptr, static_cast<DWORD>(milliseconds), QS_ALLINPUT,
                                      MWMO_INPUTAVAILABLE);
        return;
    }

    if (!m_timerChecked) {
        m_timerChecked = true;
        m_timer = CreateTimer();
    }
    if (m_timer == nullptr) {
        std::this_thread::sleep_for(remaining);
        return;
    }

    // SetWaitableTimer の負の値は「いまから 100ns 単位で」の相対指定。
    LARGE_INTEGER due = {};
    due.QuadPart = -(remaining.count() / 100);
    if (::SetWaitableTimer(m_timer, &due, 0, nullptr, nullptr, FALSE)) {
        ::WaitForSingleObject(m_timer, INFINITE);
    } else {
        std::this_thread::sleep_for(remaining);
    }
}

}  // namespace mm
