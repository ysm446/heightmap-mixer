# progress — 進捗と注意点

作成日時: 2026-08-31 05:46
更新日時: 2026-08-31 10:12

## 現在の状況

**M1（RHI の整備）完了。** Debug / Release ともにビルドが通り、起動して
コンピュートシェーダで生成したテクスチャを ImGui 上でプレビューできる状態。
シェーダのホットリロードも動作確認済み。次は M2（PBR プレビューレンダラ）。

## 完了済み

- 2026-08-31 05:46 — 方針の合意と [goals.md](goals.md) / [plan.md](plan.md) の作成。
  - 合成モデル: レイヤースタック（Quixel Mixer 風）
  - 優先順位: コア基盤のあと、地形よりマテリアル合成を先に厚くする
  - ビルド: C++20 + CMake + vcpkg
  - UI: Dear ImGui（docking ブランチ）

- 2026-08-31 05:51 — [AGENTS.md](../../AGENTS.md) / [CLAUDE.md](../../CLAUDE.md) を
  本プロジェクト（C++ / CMake / DX12）向けに書き換え。
  Web スタック前提の記述を削除し、バージョン基準を `CMakeLists.txt` の
  `project(... VERSION ...)`、検証手順を CMake ビルドに変更。

- 2026-08-31 06:18 — **M0 完了**。
  - `CMakeLists.txt` / `CMakePresets.json` / `vcpkg.json` / `.gitignore`
  - `src/core/`（Log, Window）、`src/rhi/`（Common, DescriptorHeap, Device）、
    `src/ui/`（ImGuiLayer）、`src/app/`（Application, Main）
  - DX12 デバイス / DIRECT キュー / スワップチェーン（FLIP_DISCARD, 3 バッファ）/
    フレーム同期 / RTV・SRV ディスクリプタアロケータ
  - Dear ImGui docking 統合（DX12 + Win32 バックエンド、日本語フォント読み込み）
  - デバッグレイヤー + GPU ベースバリデーション、PIX マーカー（`Frame`）
  - Agility SDK の DLL を `D3D12/` へポストビルドコピー

## 環境の実測値（2026-08-31 時点）

- Visual Studio Community 2026 (18.7.11925.98) / MSVC 14.51.36231
- Windows SDK 10.0.26100.0
- CMake 4.3.1-msvc1
- vcpkg: `C:/vcpkg`（2026-03-04）。**`VCPKG_ROOT` は未設定、PATH にも入っていない。**
  `CMakeLists.txt` 側で `VCPKG_ROOT` → `C:/vcpkg` の順にフォールバックして解決している。
- ジェネレータは **Visual Studio 18 2026（マルチコンフィグ）**。
  Ninja は VS 同梱のものしかなく、素のシェルからは開発環境変数が必要なため採用していない。
  そのため構成プリセットは `x64` の 1 つ、ビルドプリセットが `x64-debug` / `x64-release`。

## 次にやること

M2（PBR プレビューレンダラ）。

1. グラフィックス PSO のキャッシュを PipelineCache に追加。
2. 深度バッファとカメラ操作、プリミティブ（平面 / 球 / キューブ）の描画。
3. GGX 直接光。
4. HDRI の読み込みと、irradiance / prefiltered specular / BRDF LUT の生成。
5. EV100 露出と ACES トーンマップ。

ここで「マテリアルが正しく見える」基準を確定させる。

- 2026-08-31 10:12 — **M1 完了**。
  - `rhi/GpuResource`（D3D12MemoryAllocator によるテクスチャ / バッファ生成とディスクリプタ確保）
  - `rhi/UploadRing`（フレームごとに巻き戻る線形アロケータ、既定 16 MB/フレーム）
  - `rhi/DeletionQueue`（フレーム同期後に解放する遅延破棄キュー）
  - `rhi/ShaderCompiler`（DXC によるランタイムコンパイル、`shaders/` の更新検出）
  - `rhi/PipelineCache`（グローバルルートシグネチャ + コンピュート PSO のキャッシュ）
  - bindless を全面採用。起動時に Resource Binding Tier 3 を要求する
  - 疎通確認として `shaders/SmokeTest.hlsl`（fBm + 傾斜マスク）をコンピュートで評価し、
    ImGui のプレビューウィンドウへ表示

## 注意点

- **Windows SDK の `d3d12.lib` 解決**: Visual Studio ジェネレータでは SDK の lib パスが
  `find_library` の探索対象に入らず、`directx12-agility` の config が
  「D3D12.LIB import library from the Windows SDK is required」で落ちる。
  `CMakeLists.txt` でレジストリ（`KitsRoot10`）から SDK を引き、`D3D12_LIB` を
  先にキャッシュへ入れて回避している。**この処理を消すと構成が通らなくなる。**

- **フェンス同期**: 各フレームスロットの待機値は「未使用なら 0」で表し、
  `m_nextFenceValue` の単調増加値を Signal する方式にしている。
  スロットの初期値に 1 を入れる実装にすると、誰も Signal していない値を初回フレームで
  待ってしまい**確実にデッドロックする**（M0 中に実際に踏んだ）。

- **`SetBreakOnSeverity`**: デバッガ未接続で有効にすると警告のたびにプロセスが落ちるため、
  `IsDebuggerPresent()` が真のときだけ設定している。

- **文字列リテラル**: `/utf-8` を指定しているのでソースは UTF-8。
  ImGui へ渡す日本語は `u8"..."` ではなく素の `"..."` を使う
  （C++20 では `u8""` が `const char8_t*` になり `const char*` へ渡せない）。

- **VRAM の見積もり**: 8K × 4 チャンネルの評価バッファは 1 レイヤーあたり数百 MB 規模になる。
  フル解像度バッファを常駐させない方針（plan.md「評価はタイル前提の API にする」）を必ず守る。

- **Normal の合成**: lerp すると必ず破綻する。RNM を経由すること。

- **Debug 実行時のフレームレート**: GPU ベースバリデーションが有効なため負荷が上がる。
  性能を見るときは Release で確認する。

- **32 FPS 表示について**: リモートデスクトップ経由だと表示アダプタの
  リフレッシュレートが 32 Hz になるため、垂直同期が効いていると 32 FPS 上限になる。
  実機のフレームレートを見るときはローカルセッションで確認する。

- **`USE_PIX` は常時定義する**: 未定義だと `pix3.h` の呼び出しがコンパイル時に消え、
  WinPixEventRuntime.dll への依存も消えて配置されなくなる。
  Release で PIX による計測ができなくなるため、Debug / Release ともに定義している。

- **シェーダの参照先**: 実行ファイル横へコピーせず、`HM_SHADER_DIR`
  （CMake が定義。環境変数で上書き可）でソースツリーの `shaders/` を直接読む。
  配布時はこの方針を見直す必要がある。

- **リソース状態はリソース全体で 1 つ**: サブリソース単位の状態管理は未実装。
  ミップごとに別状態へ遷移させたくなった時点で拡張する。
