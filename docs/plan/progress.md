# progress — 進捗と注意点

作成日時: 2026-08-31 05:46
更新日時: 2026-08-31 11:13

## 現在の状況

**M2 完了（M2a: 直接光 / M2b: IBL）。** Debug / Release ともにビルドが通り、
球・平面・キューブを GGX 直接光 + IBL で描画し、物理カメラの露出（EV100）と
ACES トーンマップを通してビューポートに表示できる。
HDRI の読み込みとスカイボックス表示にも対応済み。次は M3（合成エンジン v1）。

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

- 2026-08-31 10:46 — **M2a 完了**。
  - `rhi/PipelineCache` にグラフィックス PSO を追加（入力レイアウトは頂点構造体ごとの列挙で管理）
  - `rhi/GpuResource` を RTV / DSV と DEFAULT ヒープバッファに対応
  - `rhi/Device` に DSV ヒープ、`BindBackBuffer`、`ExecuteImmediate` を追加
  - `renderer/Camera`（軌道カメラ）、`renderer/Mesh`（球 / 平面 / キューブ生成と GPU 転送）
  - `renderer/PreviewRenderer`（HDR シーンカラー + 深度 → トーンマップ → 表示用テクスチャ）
  - `shaders/Brdf.hlsli`、`shaders/Tonemap.hlsli`、`shaders/MeshPbr.hlsl`、
    `shaders/TonemapPass.hlsl`
  - EV100 ベースの露出（絞り / シャッター / ISO、または EV 直接指定）と
    Reinhard / ACES の切り替え
  - 高 DPI 対応（DPI 認識の有効化、フォントとスタイルのスケール、
    ウィンドウをモニタの作業領域に収める）
  - M1 の疎通確認用 `shaders/SmokeTest.hlsl` は役目を終えたため削除
    （同じ経路をトーンマップパスが通る）

- 2026-08-31 11:13 — **M2b 完了**。
  - `renderer/Environment`: equirect → キューブ → irradiance / プリフィルタ / BRDF LUT
  - `core/ImageIo`: stb による Radiance HDR (.hdr) の読み込みと PNG 書き出し
  - `rhi/GpuResource` をキューブマップ、配列、ミップ別 UAV / SRV に対応
  - `rhi/ResourceAllocator` に READBACK バッファを追加
  - シェーダ: `EnvCommon.hlsli`、`EnvSky`、`EnvEquirectToCube`、`EnvDownsample`、
    `EnvIrradiance`、`EnvPrefilter`、`EnvBrdfLut`、`Skybox`
  - `MeshPbr.hlsl` の暫定環境光を分割和近似の IBL に差し替え
  - グローバルルートシグネチャに equirect 用サンプラ（U ラップ / V クランプ）を追加
  - 開発用コマンドライン `--hdri` / `--screenshot` / `--screenshot-frame` を追加

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

M3（合成エンジン v1）。

1. レイヤースタックのデータモデル（レイヤー、マスク、ブレンドモード）。
2. GPU 評価器。出力タイル矩形と解像度を引数に取る形を最初から通す。
3. チャンネル 4 枚（BaseColor / Normal / Surface / Height）への評価。
4. ハイトベースブレンドと RNM。
5. 評価結果を M2 のプレビューマテリアルへ直結。
6. レイヤー UI。

評価結果をメッシュに貼るため、`MeshPbr.hlsl` をテクスチャ参照（bindless）に対応させる。

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

- **キューブのミップ連鎖はサブリソース単位で遷移させる**: 読むミップを
  読み取り状態、書くミップを UAV 状態にする必要がある。リソース全体を覆う SRV では
  状態が混在してしまうため、`createMipSrvs` で作ったミップ別 SRV を使う。

- **irradiance マップの中身は `E / pi`**: 余弦重み付きサンプリングの平均放射輝度。
  シェーディング側で `diffuseColor` を掛けるだけでよい。pi を二重に割らないこと。

- **環境マップに太陽を入れない**: ディレクショナルライトと二重計上になるうえ、
  256^2 のキューブでは点光源がエイリアスとファイアフライの原因になる。

- **環境の再構築はフレームの外で行う**: `Device::ExecuteImmediate` は GPU 待機を伴うため、
  UI からの要求はフラグに積み、次フレームの頭で `ProcessPendingEnvironment` が処理する。

- **描画結果の確認は `--screenshot` を使う**: リモートデスクトップ経由だと
  画面キャプチャが失敗することがある。アプリ側でビューポートを PNG に書き出せるので、
  そちらを使うほうが確実。`--hdri` と併用すれば環境の確認もできる。

- **グローバルルートシグネチャに `ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT` が必須**:
  入力レイアウトを使うグラフィックス PSO は、このフラグが無いと
  `CreateGraphicsPipelineState` が `E_INVALIDARG` で失敗する。原因が表に出にくいので注意。

- **PSO の生成失敗もキャッシュする**: 失敗を記録しないと毎フレーム再コンパイルが走り、
  ログが埋まって原因が追いにくくなる。ホットリロード時は `InvalidateAll()` で再挑戦する。

- **球の巻き順**: 球は行方向が -Y、列方向が経度なので、平面やキューブとは巻き順が逆になる。
  誤ると外向き面が裏面になり、遠側の内面が見える（暗く巨大な面が映る）。

- **高 DPI**: `ImGui_ImplWin32_EnableDpiAwareness()` はウィンドウ生成前に呼ぶ。
  呼ばないと Windows にウィンドウごと拡大され、描画解像度が半分になってぼやける。
  初期ウィンドウサイズも DPI で拡大し、`Window::Create` で実際に載ったモニタの
  作業領域に収める（`SPI_GETWORKAREA` はプライマリモニタしか見ないため使わない）。

- **既定レイアウト**: 1 フレーム目はメインビューポートの作業領域が確定していないことがある。
  ini が無い初回起動時のみ、数フレーム待ってから一度だけ既定配置を適用している。

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
