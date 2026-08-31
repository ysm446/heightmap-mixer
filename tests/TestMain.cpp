// テストの入口。領域ごとの関数を順に呼ぶだけ。
//
// 対象は「スクリーンショットでは確認できないもの」。
// UI の相互作用（ドラッグ、ホバー）と、アンドゥ履歴の段のまとめ方。

#include "TestSupport.h"

void RunUiInteractionTests();
void RunUndoHistoryTests();

int main() {
    RunUiInteractionTests();
    RunUndoHistoryTests();

    std::printf("\n%s\n", (mm::tests::g_failures == 0) ? "すべて成功" : "失敗あり");
    return (mm::tests::g_failures == 0) ? 0 : 1;
}
