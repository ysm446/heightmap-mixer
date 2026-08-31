# progress — 進捗と注意点

作成日時: 2026-08-31 05:46
更新日時: 2026-09-01 07:43

## 現在の状況

**M5b（テクスチャ一覧とファイル形式）完了。**
素材が「テクスチャ → マテリアル → レイヤー」の順に積み上がる形になり、
それぞれに一覧の置き場ができた。作った状態はプロジェクト (`.mmproj`) に保存でき、
マテリアルは単体ファイル (`.mmmat`) として持ち出せる。
形式は [reference/file-format.md](../reference/file-format.md) にまとめた。
次は M5c（フル解像度エクスポート）。

**M5a（マテリアルライブラリ）完了。**
マップ一式に名前を付けた「マテリアル」を持てるようになり、レイヤーはそれを 1 つ参照する。
サムネイル付きの一覧から選べ、テクスチャはファイル選択ダイアログで読み込む。
Megascans の ORD（チャンネルパック）と EXR にも対応した。

**M4（マスク生成）完了。**
マスクの出どころが「定数 / ノイズ / テクスチャ / 中間結果 / ペイント」の 5 系統そろった。
下地の高さ・傾斜・曲率・窪みからマスクを自動生成でき（M4a）、
さらにビューポート上でブラシを引いて直接描ける（M4b）。次は M5（入出力）。

UI はグレー基調に整理し、ルールを [design/design-guide.md](../design/design-guide.md) に置いた。

## 完了済み

- 2026-09-01 07:43 — **Application.cpp（約 3,000 行）を責務別に分割。**
  クラス定義（Application.h）は変えず、メンバ関数の定義の置き場所だけを移した。
  - コア（初期化 / フレームループ / レイアウト / ステータスバー / 設定 / 情報）は
    `Application.cpp`（約 700 行）に残し、以下を新設:
    `ApplicationViewport.cpp`（ビューポートと入力・ギズモ）、
    `ApplicationPreviewPanels.cpp`（プレビュー設定 / ライティングと露出）、
    `ApplicationLayerPanel.cpp` / `ApplicationMaterialPanel.cpp` /
    `ApplicationTexturePanel.cpp`（各パネル）、
    `ApplicationDocument.cpp`（アンドゥ文書）、
    `ApplicationFileWork.cpp`（メニューと保留ファイル作業）。
  - 匿名名前空間の共有ヘルパは `ApplicationUiHelpers.h`（内部ヘッダ、inline 化）へ集約。
    コア専用の定数（ウィンドウ初期サイズなど）はコア側の匿名名前空間に残した。

- 2026-09-01 07:28 — **コードレビューと不具合修正（全域）。**
  rhi / compositor / renderer / app・io・ui をレビューし、critical と bug を中心に修正。
  詳細は [changelog](../changelog.md) の同日時の項を参照。
  - ディスクリプタの遅延解放（`Device::DeferRelease`）を導入し、
    手動の `WaitForGpu` に頼る解放を全廃した。
  - 未修正の改善候補（提案のみ）:
    `DispatchCount` / `TransitionIfNeeded` などの共通ヘルパの rhi への集約、
    `ToUtf8` / `FromUtf8` の一本化（core へ）、プレビュー既定値の構造体化、
    ラフネス下限の統一（0.03 / 0.05 / 1e-3 が混在）、
    `MeshPbr` の VS / DS ディスプレイスメント式の共通化。

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

- 2026-08-31 16:02 — **パネルをドックレイアウトへ移行**。
  - `Application::BuildDefaultLayout`（`DockBuilder` で既定配置を組む）を追加し、
    絶対座標で置く `SetDefaultWindowRect` を廃止
  - `表示 > レイアウトをリセット` を追加
  - 右のパネルが画面外へはみ出して見切れる問題の修正

- 2026-08-31 17:12 — **M5a 完了**（マテリアルライブラリ）。
  - `compositor/MaterialLibrary`: マテリアル（マップ一式 + 定数 + サムネイル）の管理
  - `shaders/MaterialThumbnail.hlsl`: 正面を向いた球を解析的に解いて描くサムネイル
  - `MaterialLayer` から `LayerTextures` を廃し、`MaterialAssetId material` を持たせた。
    マスク用テクスチャは `LayerMask::texture` へ移動
  - 評価器はマテリアルからマップと定数を引く。マテリアルがあればレイヤー側の
    色・サーフェスの値は使わない
  - `core/FileDialog`（IFileDialog）。テクスチャ（複数可）と HDRI をダイアログで開く。
    パスの手入力欄は廃止
  - マテリアルパネル（レイヤーのとなりのタブ）。サムネイルの一覧、マップの割り当て
  - M5b に向けた土台も入れた（`nlohmann-json`、`SaveGray8Png`、
    `PaintMaskStore::ReadPixels/AddFromPixels/Clear`、`CameraState`、`Window::RequestClose`）

- 2026-08-31 18:04 — **チャンネルパックと EXR**（M5a への追加）。
  - スカラーのマップを `MapSlot`（テクスチャ + `TextureChannel`）に変更。
    Megascans の `_ORD`（O=AO / R=Roughness / D=Displacement）を 1 枚のまま使える
  - `MaterialLibrary::AssignOrdTexture` と、マテリアルパネルの `ORD` 行
  - チャンネル指定は 4bit ずつ 1 つの uint へ詰めてシェーダへ渡す
    （`MM_CHANNEL_SLOT_*` / `PackChannel`）
  - `core/ImageIo` に `LoadExrImage`（tinyexr）を追加
  - `TextureLibrary` が EXR を `R16G16B16A16_FLOAT` のまま保持する

- 2026-08-31 18:52 — **プロジェクト名を material-mixer へ変更**。
  - 表に出る名前（実行ファイル、ウィンドウタイトル、vcpkg.json、ini、ドキュメント）と
    内部の識別子（`namespace mm`、`MM_*` マクロ、HLSL の `MM_*`）をまとめて変更
  - プロジェクトファイルの拡張子も `.mmproj` にした（M5b で使う）
  - **リポジトリのフォルダ名は未変更**。`d:/GitHub/heightmap-mixer` のまま。
    フォルダを変えたら `build/` を作り直すこと（`MM_SHADER_DIR` に絶対パスが焼かれている）

- 2026-08-31 15:12 — **M5b 完了**（テクスチャ一覧 / プロジェクトとマテリアルの保存）。
  - **テクスチャパネル**（レイヤー・マテリアルと同じ枠の 3 つめのタブ）。
    サムネイル一覧、解像度 / ミップ / 形式 / 参照数 / 場所、名前の変更、削除。
    サムネイルからマテリアルのマップ欄へ**ドラッグ＆ドロップ**で割り当てられる
    （ペイロード `MM_TEXTURE`）。画像の読み込み口はマテリアルパネルからここへ移した。
  - `io/ProjectIo`: `.mmproj` と `.mmmat` の読み書き（nlohmann-json）。
    仕様は [reference/file-format.md](../reference/file-format.md)。
    - **プロジェクトにはマテリアルの構造を丸ごと埋め込む。** 開くのに `.mmmat` は要らない。
    - 画像は参照だけ持ち、パスはファイルからの相対で書く。
    - ペイントマスクは `<名前>.assets/paint_NNNN.png` へ書き出す。
    - 例外は使わないので、パースは `json::parse(..., allow_exceptions=false)` で受け、
      型が合わない項目は既定値へ落とす。
  - ファイルメニュー（新規 / 開く / 保存 / 名前を付けて保存 / 終了）と、
    マテリアルパネルの書き出し / 読み込み。要求は積むだけで、
    実処理は `Application::ProcessPendingFileWork()` がフレームの外で行う。
  - `TextureLibrary`: 同じパスの画像は読み直さず既存の ID を返すようにした。
    `FindByPath` / `FindMutable` / `Clear` を追加。
  - `Window::SetTitle`。タイトルバーに開いているプロジェクト名を出す。
  - 起動オプション `--project`（開く）と `--save-project`（開発用の保存して終了）。
  - **未保存の変更があるまま閉じたときの確認は入れていない**（下の注意点を参照）。

- 2026-08-31 15:49 — **ステータスバーとドロップ、EXR 表示の修正**。
  - **ステータスバー**（画面下端）。左に直近の通知とモード、右に
    プロジェクト名 / レイヤー・マテリアル・テクスチャの数 / 合成解像度 / FPS。
    `core/Log` に出力先（`SetLogSink`）を足し、ログをそのまま通知として出す。
    **これまで警告やエラーはデバッグ出力にしか出ておらず、GUI からは見えなかった。**
  - **エクスプローラからのドロップ**（`WM_DROPFILES`）。拡張子で行き先を振り分ける
    （画像 → テクスチャ / `.mmproj` → プロジェクト / `.mmmat` → マテリアル /
    `.hdr` → 環境）。扱えない拡張子は警告を出す。
  - **EXR のサムネイルが暗すぎた問題の修正。** `shaders/TexturePreview.hlsl` を追加し、
    リニアなテクスチャは読み込み時に sRGB へ焼いた 128^2 の表示用テクスチャを作る。
  - 読み込んだテクスチャを選択して一覧の枠内へ送るようにした（`SetScrollHereY`）。
  - 既定レイアウトを組んだあと「レイヤー」を前面にする（タブ 3 枚の並びが
    組んだ順に左右されないようにするため）。

- 2026-09-01 00:30 — **ハイトの基準面と「起伏の強さ」**。
  - ハイトの式を `heightBase + (src - kHeightPivot) * heightGain` に変更した
    （`kHeightPivot = 0.5`）。**基準の高さと起伏の強さが直交する。**
    起伏を変えても平均の高さが動かないので、勝敗と凹凸を別々に決められる。
  - `MaterialLayer::heightGain` を `NoiseParams` の外に出した。
    **これまで寄与の量はノイズの `amount` が兼ねていて、ソースがテクスチャのときは
    UI に出ていなかった**（マテリアルのハイトマップの強さを調整できなかった）。
    ノイズの行からは「量」を外し、`起伏の強さ` に一本化した。
  - **基準面はマップの平均ではなく 0.5 固定。** ディスプレイスメントマップは
    「中間グレーが変位ゼロ」の慣習で作られるため。判断の経緯は
    [compositing.md](../design/compositing.md) の「ハイトの基準面を 0.5 に固定する理由」。
  - **ファイル形式を版 2 へ。** `gain` の有無で旧形式を判定し、
    `base' = base + 0.5 * gain` で移行する（定数ソースは対象外）。厳密に同じ値になる。
    版を上げたのは、`base` の意味が変わり古いビルドが黙って違う絵を出すため。
  - UI の `出どころ` を `ソース` に変更（ハイト / マスク）。
  - 既定レイヤーの `heightBase` を 0.5 にした。追加したてのレイヤーが
    既存のレイヤーより極端に低く沈まないようにするため。

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

M5c（フル解像度エクスポート）。

1. タイル評価で 4K / 8K の出力を作る（フル解像度バッファは常駐させない）。
2. チャンネルパッキングの設定（ORM / ORD など出力側の並びを選べるようにする）。
3. 出力プリセット（どのマップをどの形式で出すか）と、出力先の記憶。
4. **未保存の変更があるまま閉じたときの確認**は M5b でも入れなかった。
   変更フラグを持つ場所（レイヤー / マテリアル / テクスチャ / ペイント）が分かれているので、
   `MaterialStack::Revision()` のような通し番号を全体に広げるかを先に決める。

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

- **シェーダの参照先**: 実行ファイル横へコピーせず、`MM_SHADER_DIR`
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
- **ウィンドウは作業領域に入りきらなくても縮めない**（`Window::ResizeClient`）。
  縮めるとクライアント領域を実ピクセルで固定した意味が無くなる。位置だけ寄せる。
  スナップや最大化のあとは `設定 > ウィンドウ > 既定の大きさに戻す` で戻す。
- **高 DPI 対応はウィンドウ生成前に有効化する**。忘れると OS がウィンドウごと
  ビットマップ拡大し、描画解像度が落ちてぼやける。
  **UI の拡大率は既定ではモニタの DPI に追従させない**（100% 固定）。
  クライアント領域を実ピクセルで固定しているので、
  追従させると作業面積がモニタ設定で変わってしまう。
  高 DPI では文字が小さくなるため、`設定 > UI` で使う人が選べるようにしてある。
- **拡大率の変更でフォントアトラスを作り直さない。** ImGui 1.92 では
  `style.FontScaleDpi` で動的に拡縮されるので、フォントは基準サイズ(17px)で
  1 回だけ読む。作り直すと GPU 待機とディスクリプタの張り替えが要る。
- **`ImGui::Image` は入力を消費しない**。`InvisibleButton` を重ねること。
- **パネルを絶対座標で置かない**。ウィンドウの大きさが変わったときや、
  別の大きさで保存された ini を読んだときに画面外へはみ出す。
  ドックに収めれば、ウィンドウの大きさに必ず追従する。
- **既定のドックレイアウトを組むかの判定は `DockSpaceOverViewport` より前に行う**。
  後だとノードが作られてしまい、`DockBuilderGetNode` が必ず非 null になる。
- **`DockBuilderSplitNode` の前に `DockBuilderSetNodeSize` を呼ぶ**。
  呼ばないと分割比が当てにならない。
- **ドックのノードサイズは ini に絶対値で残る**。小さいウィンドウで開くと
  釣り合いが崩れることがあるので、`表示 > レイアウトをリセット` で組み直せるようにしてある。

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

### アセット（詳細は [design/compositing.md](../design/compositing.md)）

- **同じ意味の値を 2 か所に置かない**。マテリアルを割り当てたレイヤーでは、
  レイヤー側の色とサーフェスの値は使わない（UI からも隠す）。
  掛け合わせにすると、どちらが効いているのか分からなくなる。
- **マテリアルを削除したら、参照していたレイヤーを「なし」へ戻す**。
  無効な ID を残すと、次に同じ番号が払い出されたときに別のマテリアルが付く。
- **サムネイルは変更があったときだけ作り直す**。生成は `ExecuteImmediate` を使うので
  フレームの外。毎フレーム作ると GPU 待機が入って描画が止まる。
- **`CreateDialog` という名前は使えない**。Windows のマクロ（`CreateDialogW`）と衝突する。
- **EXR は 8bit へ落とさない**。ハイトに階段が出る。`R16G16B16A16_FLOAT` のまま持つ。
- **float テクスチャは sRGB 用の SRV を別に張らない**（中身がすでにリニアなので）。
  linear と同じインデックスを返しているので、破棄のときに二重解放しないよう
  `isFloat` で分岐する。

### ファイル入出力（詳細は [reference/file-format.md](../reference/file-format.md)）

- **`std::filesystem::relative` はドライブが違うと空を返す。** 相対にできないときは
  絶対パスのまま書く。落とさずに書き切ることを優先する。
- **`path::string()` はロケール依存**（Windows では ACP）。JSON も ImGui も UTF-8 なので、
  `u8string()` を経由して変換する。日本語のパスやマテリアル名で化ける。
- **nlohmann-json は既定で例外を投げる。** 例外を使わない方針なので、
  `json::parse(..., allow_exceptions=false)` で受けて `is_discarded()` を見る。
  値を取り出すときも `is_number()` などで型を確かめてから。
- **読み込みのあとは `PaintMaskStore::RequestResolution` も揃える。**
  揃えないと、次の `ProcessPendingWork` が読み込む前の解像度へ戻そうとして
  全マスクをリサンプルする。
- **保存するのはレイヤーが参照しているペイントマスクだけ。**
  `PaintMaskStore` は一覧を公開していないので、レイヤーから辿って集めている。
- **ファイル内の ID は保存のたびに 1 から振り直す。** 実行中の ID をそのまま書くと、
  削除で番号が飛んだ読みにくいファイルになる。読み込み側で対応表を作って解決する。
- **同じパスの画像は読み直さない**（`TextureLibrary::Load` が既存の ID を返す）。
  `_ORD` のように複数のマップが 1 枚を指すのが普通なので、これが無いと重複する。
- **読み書きはフレームの外で行う。** ダイアログはフレームの中で出してよいが、
  選ばれたパスは保留し、`ProcessPendingFileWork()` が次のフレームの頭で処理する。
  テクスチャの削除も同じ（ディスクリプタを返すので `WaitForGpu` してから）。

### UI の通知とドロップ

- **ImGui はテクスチャの値をそのままバックバッファへ書く。** リニアな内容（EXR）を
  `ImGui::Image` に渡すと極端に暗くなる。一覧に出すものは sRGB へ焼いてから渡す。
- **`DROPFILES` は `shellapi.h` ではなく `ShlObj_core.h` にある**（現行 SDK）。
  `DragQueryFileW` / `DragAcceptFiles` / `DragFinish` は `shellapi.h`。
- **`DragQueryFileW` が返す長さは終端を含まない。** バッファには +1 して渡す。
- **ログのシンクは持ち主より先に外す**（`SetLogSink({})`）。
  `Application` を掴んだままだと、破棄後のログで壊れたポインタを呼ぶ。
- **タブの並びと前面は submit した順で決まる**（ini に配置が無いとき）。
  `ImGui::SetWindowFocus` / `SetNextWindowFocus` や `DockBuilder` のドック順では動かない。
  リポジトリ直下の `material_mixer_imgui.ini` は古い配置を持っているので、
  **そこを作業ディレクトリにして起動すると既定レイアウトの見え方が変わる**。
  既定の確認は ini の無いディレクトリで行うこと。
- **ステータスバーはドックスペースより前に描く。** `BeginViewportSideBar` は
  メニューバーと同じく作業領域を狭めるので、後に置くとドックがバーの下へ潜る。

### 開発環境（リモートデスクトップ）

- **描画結果の確認は `--screenshot` を使う**: 画面キャプチャが失敗することがある。
  アプリ側でビューポートを PNG に書き出せるので、そちらが確実。
- **32 FPS 上限は正常**: リモート経由だと表示アダプタのリフレッシュレートが 32 Hz。
  垂直同期が効いていれば 32 FPS で頭打ちになる。実機の性能はローカルセッションで見る。
- **Debug 実行時のフレームレート**: GPU ベースバリデーションが有効なため負荷が上がる。
  性能を見るときは Release で確認する。
- **ビルド前に起動中のインスタンスを閉じる**: exe がロックされてリンクに失敗する。
