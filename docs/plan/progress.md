# progress — 進捗と注意点

作成日時: 2026-08-31 05:46
更新日時: 2026-08-31 15:24

## 現在の状況

**M4（マスク生成）完了。**
マスクの出どころが「定数 / ノイズ / テクスチャ / 中間結果 / ペイント」の 5 系統そろった。
下地の高さ・傾斜・曲率・窪みからマスクを自動生成でき（M4a）、
さらにビューポート上でブラシを引いて直接描ける（M4b）。次は M5（入出力）。

UI はグレー基調に整理し、ルールを [design/design-guide.md](../design/design-guide.md) に置いた。

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

- 2026-08-31 10:12 — **M1 完了**。
  - `rhi/GpuResource`（D3D12MemoryAllocator によるテクスチャ / バッファ生成とディスクリプタ確保）
  - `rhi/UploadRing`（フレームごとに巻き戻る線形アロケータ、既定 16 MB/フレーム）
  - `rhi/DeletionQueue`（フレーム同期後に解放する遅延破棄キュー）
  - `rhi/ShaderCompiler`（DXC によるランタイムコンパイル、`shaders/` の更新検出）
  - `rhi/PipelineCache`（グローバルルートシグネチャ + コンピュート PSO のキャッシュ）
  - bindless を全面採用。起動時に Resource Binding Tier 3 を要求する
  - 疎通確認として `shaders/SmokeTest.hlsl`（fBm + 傾斜マスク）をコンピュートで評価し、
    ImGui のプレビューウィンドウへ表示

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

- 2026-08-31 11:37 — **M3a 完了**。
  - `compositor/MaterialLayer`（レイヤー定義）、`compositor/MaterialStack`（スタック）
  - `compositor/MaterialEvaluator`（GPU 評価器。タイル矩形と解像度を引数に取る）
  - 出力 4 枚: BaseColor (R11G11B10F) / Normal (RG16F) / Surface (RGBA8) / Height (R16F)
  - `shaders/CompositeCommon.hlsli`（RNM、ハイトブレンド、マスクのレベル調整）
  - `shaders/CompositeLayer.hlsl`（レイヤー 1 枚ぶんの合成）
  - ハイトとマスクの出どころに fBm ノイズを用意し、法線はハイトの勾配から作る
  - `MeshPbr.hlsl` を合成結果のサンプリングに対応（タンジェント空間法線を適用）
  - レイヤー UI（追加 / 複製 / 削除 / 並べ替え / 有効切り替え / 各パラメータ）
  - パネル配置を組み直し（左: レイヤー、中央: ビューポート、右: 設定と情報）

- 2026-08-31 12:02 — **M3b 完了**。
  - `core/ImageIo` に LDR 画像（PNG / TGA / JPG）の読み込みを追加
  - `compositor/TextureLibrary`: 読み込み済みテクスチャの保持と bindless インデックスの払い出し。
    リソースは TYPELESS で作り、UNORM と UNORM_SRGB の 2 つの SRV を張る
  - `shaders/TextureMips.hlsl`: 読み込み時のミップ生成
  - レイヤーに 7 つのテクスチャスロット
    （ベースカラー / 法線 / ラフネス / メタルネス / AO / ハイト / マスク）
  - `CompositeLayer.hlsl` をテクスチャサンプリングに対応。
    コンピュートでは暗黙の LOD が使えないため、出力テクセルが張る UV 幅から
    ミップレベルを計算して `SampleLevel` する
  - `TextureDesc` に `uavFormat` を追加（TYPELESS リソース用）
  - 起動オプション `--texture <path>`（繰り返し可）

- 2026-08-31 12:09 — **ドキュメント整理**。
  - `README.md` を新規作成（概要 / ビルド / 実行 / 操作 / 構成 / ドキュメント一覧）
  - `docs/design/` を新設し、`plan.md` に溜まっていた実装設計を切り出した
    （`rhi.md` / `rendering.md` / `compositing.md`）
  - `plan.md` を計画の入口として整理（技術選定 / 構成 / マイルストーン / 保留事項）
  - `plan.md` のハイトブレンドの式が実装と食い違っていたので design 側で修正
  - 本ファイルの注意点を分類し、重複と矛盾（既定レイアウトの適用条件）を解消

- 2026-08-31 12:34 — **M4a 完了**。
  - `MaskSource` に中間結果由来のマスクを追加（下地の高さ / 傾斜 / 曲率 / 窪み）
  - `shaders/CompositeMask.hlsl`: 下地の Height からマスクを作る専用パス
  - 評価のループを**レイヤー優先**に変更（外側レイヤー、内側タイル）。
    近傍参照を伴うマスクはタイル優先だと境界に継ぎ目が出る
  - `Evaluate` の引数をタイル 1 個からタイル群へ変更
  - ノイズの種類を追加（尾根状 / セル状 Worley）。ハイトとマスクの双方で選べる
  - マスクにカーブ（コントラスト）を追加
  - 既定スタックに「窪みに苔が生える」レイヤーを追加

- 2026-08-31 13:32 — **M4b 完了**（ペイントマスク）。
  - `compositor/PaintMask`（`PaintMaskStore`）: ペイントマスク（R8_UNORM）の管理、
    ブラシ適用、塗りつぶし、アンドゥ / リドゥ、解像度変更のリサンプル
  - `MaskSource::Paint` を追加。`LayerMask::paint` がマスクの ID を持つ
  - `shaders/PaintBrush.hlsl`: 線分ストロークのブラシ。標本化は groupshared で共有
  - `shaders/PaintFill.hlsl`: 一様な値での塗りつぶし
  - `MeshPbr.hlsl` を MRT 化し、2 枚目へマテリアル UV と被覆を書き出す。
    `rhi::GraphicsPipelineDesc` に `rtvFormat1` を追加
  - `PreviewRenderer` に UV バッファ（`m_materialUv`）と
    `PrepareUvBufferForRead`（ブラシへ渡す SRV とビューポートサイズ）を追加
  - レイヤー UI にペイント欄（作成 / モード切替 / 半径・強さ・減衰 / 消しゴム /
    全消去・全塗り / アンドゥ・リドゥ / 解像度 / 破棄）
  - ビューポート: ペイントモード中は左ドラッグで塗り、右ドラッグで消す。
    軌道は Alt + 左ドラッグへ退避。カーソルにブラシ半径の円を重ねる
  - レイヤーの複製ではペイントマスクを中身ごと複製し、削除では破棄する
  - `kInvalidTextureIndex` を `TextureLibrary.h` から `MaterialLayer.h` へ移した

- 2026-08-31 14:36 — **UI の整理**（グレー基調 / プロパティ行の統一）。
  - `src/ui/UiStyle`: テーマ（`ApplyTheme`）と、プロパティ行のヘルパー（`Property*`）
  - 配色をグレー基調に作り直した。アクセントは 1 色（`#96A3AD`）だけ
  - 全パネルの設定値を「パラメータ名：値」の 2 列テーブルへ揃えた
  - 既定値マーカー（`ResetDot`）を追加。既定値は設定構造体の初期値を渡す
  - レイヤーの並べ替えをドラッグ＆ドロップにし、`MaterialStack::MoveTo` を追加。
    「上へ」「下へ」ボタンは役目が重なるので廃止
  - ウィンドウのクライアント領域を 1920x1080 に固定
    （`AdjustWindowRectExForDpi`）。UI スケールは 1.0 固定
  - `Device::RequestBackBufferCapture` と起動オプション `--screenshot-ui` を追加
  - [design/design-guide.md](../design/design-guide.md) を新規作成し、
    `CLAUDE.md` / `AGENTS.md` に「UI 実装」の節を足して参照させた

- 2026-08-31 15:24 — **座標系を右手系へ統一し、座標軸ギズモを追加**。
  - `Camera` のビュー / 投影を `XMMatrixLookAtRH` / `XMMatrixPerspectiveFovRH` へ変更。
    ラスタライザを `FrontCounterClockwise = TRUE` に固定
  - `Camera::Basis()`（ビュー行列と同じ基底をワールド座標で返す）を追加
  - `Camera::Orbit` の引数を画面のドラッグ量として整理し、ヨーの符号を反転
  - ビューポート左下に座標軸ギズモ（`DrawAxisGizmo`）

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

M5（入出力）。

1. テクスチャインポートの拡充（EXR）。
2. プロジェクトの保存と読み込み（JSON + 参照アセット）。
   **ペイントマスクは手続きで再現できないので、画像として一緒に保存する必要がある。**
3. フル解像度エクスポート（タイル評価 + チャンネルパッキング + 出力プリセット）。
4. 配布形態（シェーダの同梱方法）の見直し。

## 注意点

実装の設計そのものは [design/](../design/) にまとめてある。
ここには「踏んだ罠」と環境固有の事情を残す。

### ビルドと構成

- **Windows SDK の `d3d12.lib` 解決**: Visual Studio ジェネレータでは SDK の lib パスが
  `find_library` の探索対象に入らず、`directx12-agility` の config が
  「D3D12.LIB import library from the Windows SDK is required」で落ちる。
  `CMakeLists.txt` でレジストリ（`KitsRoot10`）から SDK を引き、`D3D12_LIB` を
  先にキャッシュへ入れて回避している。**この処理を消すと構成が通らなくなる。**

- **`USE_PIX` は常時定義する**: 未定義だと `pix3.h` の呼び出しがコンパイル時に消え、
  WinPixEventRuntime.dll への依存も消えて配置されなくなる。
  性能は Release で測るものなので、Release で計測できないと意味がない。

- **文字列リテラル**: `/utf-8` を指定しているのでソースは UTF-8。
  ImGui へ渡す日本語は `u8"..."` ではなく素の `"..."` を使う
  （C++20 では `u8""` が `const char8_t*` になり `const char*` へ渡せない）。

- **シェーダの参照先**: 実行ファイル横へコピーせず、`HM_SHADER_DIR`
  （CMake が定義。環境変数で上書き可）でソースツリーの `shaders/` を直接読む。
  exe 単体を別マシンへ持っていっても動かない。配布形態は M5 で見直す。

### DirectX 12（詳細は [design/rhi.md](../design/rhi.md)）

- **フェンス同期**: スロットの初期値に 1 を入れると、誰も Signal していない値を
  初回フレームで待って**確実にデッドロックする**（M0 中に実際に踏んだ）。
- **ルートシグネチャに `ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT` が必須**。
  無いとグラフィックス PSO 作成が `E_INVALIDARG` で落ちる。原因が表に出にくい。
- **PSO の生成失敗もキャッシュする**。しないと毎フレーム再コンパイルでログが埋まる。
- **ミップ連鎖の生成はサブリソース単位で遷移させる**。
  リソース全体を覆う SRV では状態が混在する。
- **`Device::ExecuteImmediate` はフレームの外でしか呼べない**。
  UI からの要求はフラグに積み、次フレームの頭で `ProcessPendingWork` が処理する。
- **`SetBreakOnSeverity`**: デバッガ未接続で有効にすると警告のたびにプロセスが落ちるため、
  `IsDebuggerPresent()` が真のときだけ設定している。
- **リソース状態はリソース全体で 1 つ**（ミップ連鎖の生成だけ例外）。

### 描画（詳細は [design/rendering.md](../design/rendering.md)）

- **座標系は右手系 Y-up**。LH の行列を使うと画面が左右反転し、+X が画面左に出る。
  対称なプリミティブでは気づきにくいので、規約側で固定しておく。
- **右手系ではラスタライザを `FrontCounterClockwise = TRUE` にする**。
  LH のままの設定を残すと、外向きの面がすべて裏面判定になって消える。
- **右手系にすると画面の右が +X になる**。軌道のヨーは符号を反転しないと、
  ドラッグした向きと逆に内容が回る。

- **irradiance マップの中身は `E / pi`**。pi を二重に割らないこと。
- **環境マップに太陽を入れない**。ディレクショナルライトと二重計上になる。
- **球の巻き順は平面・キューブと逆**。誤ると遠側の内面が見える。
- **高 DPI 対応はウィンドウ生成前に有効化する**。忘れると OS がウィンドウごと
  ビットマップ拡大し、描画解像度が落ちてぼやける。
  ただし **UI の拡大率はモニタの DPI に追従させない**（1.0 固定）。
  クライアント領域を実ピクセルで固定しているので、
  追従させると作業面積がモニタ設定で変わってしまう。
- **`ImGui::Image` は入力を消費しない**。`InvisibleButton` を重ねること。
- **既定レイアウトは `ImGuiCond_FirstUseEver` で毎フレーム流す**。
  「ini が無い初回起動だけ適用」にすると、後から追加したパネルが
  既存 ini の環境で配置されず重なる。

### 合成（詳細は [design/compositing.md](../design/compositing.md)）

- **マスクは高さと同じ土俵で競合する**。マスク 1.0 は高さを無視して全面を覆う。
  高さで勝敗を決めたいときは双方 0.5 付近にする。
- **Normal は lerp すると必ず破綻する**。RNM を経由すること。
- **法線の勾配スケール**: 生の `d(height)/d(uv)` は極端に急な法線になる。
  `kNormalGradientScale` で丸めている。
- **コンピュートシェーダでは暗黙の LOD が使えない**。`SampleLevel` を使う。
- **タイル評価は常用する**。使わない経路は壊れたままになる。
- **評価のループはレイヤー優先**。中間結果由来のマスクは近傍を参照するため、
  タイル優先で回すと未評価の隣タイルを読んで境界に継ぎ目が出る。
- **中間結果由来のマスクは別パスで計算する**。合成パスは Height を UAV として
  書き換えるので、同じディスパッチで近傍を読むと競合する。
- **VRAM の見積もり**: 8K × 4 チャンネルは 1 レイヤーあたり数百 MB 規模。
  フル解像度バッファを常駐させない方針を必ず守る。

### ペイント

- **ペイントマスクはレイヤーの UV スケールを掛けない座標で引く**。
  掛けると、描いた場所と出る場所がずれる。
- **UV バッファは 1 フレーム前の内容を読む**。ブラシは合成の評価より前に流すため。
  描き味に出るほどの差はないので、そのために同期を入れない。
- **UV の距離は 0 / 1 の境界をまたぐ**。マスクはメッシュ上でタイリングするので、
  `min(d, 1 - d)` で巻き戻して測らないと継ぎ目でストロークが切れる。
- **スカイボックスの前にレンダーターゲットを束ね直す**。
  メッシュは MRT（シーンカラー + UV）だが、スカイボックスの PSO は 1 枚しか持たない。
- **UV バッファのクリア値はリソース生成時の値と揃える**。
  被覆 0 でクリアするので `clearColor` のアルファも 0 にしてある。
- **ペイントの要求はフレーム内の 1 か所でまとめて記録する**。
  UI から直接コマンドリストへ積まない。テクスチャ生成やアンドゥの
  履歴確保は UI（フレームの外）で済ませ、コピーとディスパッチだけを frame 内で流す。
- **ペイントマスクを破棄するときは GPU 待機する**。
  ディスクリプタを解放するため、`PaintMaskStore::Remove` はフレームの外で呼ぶ。

### UI（詳細は [design/design-guide.md](../design/design-guide.md)）

- **UI の確認は `--screenshot-ui` を使う**。OS の画面キャプチャは
  前面化に失敗して別ウィンドウを掴むことがあり、
  DPI 非対応のプロセスから撮ると縮んだ絵になる。
- **プロパティ行の ID はラベル文字列から作る**。同じラベルを同じテーブルへ 2 回置くと
  ID が衝突して片方が動かなくなる。節ごとにテーブルを分けること。
- **`SectionHeader` はプロパティテーブルの外で呼ぶ**。テーブルの行として呼ぶと
  列の幅計算が崩れる。
- **レイヤー一覧のドラッグ結果はループの外で反映する**。
  走査中に `MoveTo` を呼ぶと、その場で並びが変わって描画と食い違う。
- **バックバッファのコピーはフレームの中で record する**。
  フリップモデルでは Present 後の内容が破棄されうる。

### 開発環境（リモートデスクトップ）

- **描画結果の確認は `--screenshot` を使う**: 画面キャプチャが失敗することがある。
  アプリ側でビューポートを PNG に書き出せるので、そちらが確実。
- **32 FPS 上限は正常**: リモート経由だと表示アダプタのリフレッシュレートが 32 Hz。
  垂直同期が効いていれば 32 FPS で頭打ちになる。実機の性能はローカルセッションで見る。
- **Debug 実行時のフレームレート**: GPU ベースバリデーションが有効なため負荷が上がる。
  性能を見るときは Release で確認する。
- **ビルド前に起動中のインスタンスを閉じる**: exe がロックされてリンクに失敗する。
