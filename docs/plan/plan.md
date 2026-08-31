# plan — 実装方針と優先順位

作成日時: 2026-08-31 05:46
更新日時: 2026-08-31 12:34

進捗管理の入口。実装の詳細な設計は [docs/design/](../design/) に置く。

- [design/rhi.md](../design/rhi.md) — bindless、ルートシグネチャ、リソース管理、シェーダ
- [design/rendering.md](../design/rendering.md) — 描画の流れ、露出とトーンマップ、IBL、行列の規約
- [design/compositing.md](../design/compositing.md) — チャンネル定義、ハイトブレンド、RNM、タイル評価

## 決定済みの技術選定

| 項目 | 選択 |
| --- | --- |
| 言語 | C++20 |
| ビルド | CMake + vcpkg（manifest モード） |
| グラフィックス API | DirectX 12（SM 6.6、bindless） |
| シェーダ | HLSL / DXC（実行時コンパイル、ホットリロード） |
| UI | Dear ImGui（docking ブランチ） |
| 合成モデル | レイヤースタック（Quixel Mixer 風） |
| 対象 OS | Windows のみ |

導入済みの依存: DirectX-Headers, DirectX-Agility-SDK, D3D12MemoryAllocator,
WinPixEventRuntime, DirectXShaderCompiler, stb, Dear ImGui。
今後の想定: tinyexr（EXR 入出力）、nlohmann-json（プロジェクト保存）。

## ディレクトリ構成

```
CMakeLists.txt / CMakePresets.json / vcpkg.json
src/
  app/          アプリ本体、エントリポイント、UI パネル
  compositor/   レイヤースタックの定義、GPU 評価器、テクスチャライブラリ
  core/         ログ、Win32 ウィンドウ、画像入出力
  renderer/     PBR プレビュー描画、カメラ、メッシュ、IBL 環境
  rhi/          DX12 ラッパ
  ui/           Dear ImGui の統合
  terrain/      ハイトマップ地形（M6 で追加）
shaders/        HLSL
assets/         同梱アセット
docs/
tests/
```

## マイルストーン

### M0 — 基盤の立ち上げ（完了）
CMake + vcpkg のプロジェクト構成、Win32 ウィンドウ、DX12 デバイスと
スワップチェーン、フレームループ、Dear ImGui（docking）の統合、
デバッグレイヤーと PIX マーカー。

### M1 — RHI の整備（完了）
D3D12MemoryAllocator、ディスクリプタヒープアロケータ、アップロードリングバッファ、
遅延破棄キュー、PSO / ルートシグネチャのキャッシュ、
DXC によるシェーダコンパイルとホットリロード、bindless の全面採用。

### M2 — PBR プレビューレンダラ（完了）
**ここで「マテリアルが正しく見える」基準を確定させた。**

- **M2a**: グラフィックス PSO キャッシュ、深度バッファ、軌道カメラ、
  プリミティブ（球 / 平面 / キューブ）、GGX 直接光、HDR シーンカラー、
  EV100 露出、ACES トーンマップ、高 DPI 対応。
- **M2b**: IBL。HDRI 読み込み、equirect → キューブマップ変換、
  irradiance、prefiltered specular、BRDF LUT、スカイボックス表示。

### M3 — 合成エンジン v1（完了）

- **M3a**: レイヤースタックのデータモデル、GPU 評価器（タイル引数付き）、
  単色レイヤーとノイズによるハイト・マスク、ハイトブレンド、RNM、
  マスクのレベル調整と反転、評価結果をプレビューマテリアルへ直結、レイヤー UI。
- **M3b**: テクスチャレイヤー。画像の読み込みとテクスチャライブラリ、
  チャンネルごとのテクスチャ指定、マスクテクスチャ、ミップ生成。

### M4 — マスク生成

- **M4a**（完了）: ノイズの拡充（尾根状 / セル状 Worley）、
  **合成中間結果由来のマスク（高さ / 傾斜 / 曲率 / 窪み）**、マスクのカーブ。
  「窪みにだけ苔を生やす」「急斜面にだけ岩を出す」が書けるようになった。
- **M4b**（次）: ペイントマスク（ブラシ）。
  ビューポートのクリック位置からマテリアルの UV を求める仕組みが要る。

### M5 — 入出力
テクスチャインポートの拡充（EXR）、
プロジェクトの保存と読み込み（JSON + 参照アセット）、
フル解像度エクスポート（タイル評価 + チャンネルパッキング設定 + 出力プリセット）。
配布形態（シェーダの同梱方法）もここで見直す。

### M6 — 地形
ハイトマップインポート（16bit PNG / RAW / EXR）、LOD 付き地形描画、
マテリアルのスプラット適用、地形属性を M4 のマスク入力へ接続、地形ペイント。

## 開発用のコマンドライン

```
heightmap_mixer.exe [--hdri <path>] [--texture <path>]...
                    [--screenshot <path>] [--screenshot-frame <n>]
```

`--texture` は繰り返し指定でき、起動時にテクスチャライブラリへ読み込む。

`--screenshot` はビューポートの内容を PNG に書き出して終了する。
画面キャプチャに頼らず描画結果を確認できるため、リモート環境や自動確認で使う。

## 判断を保留している点

- 地形 LOD の方式（CDLOD / ジオメトリクリップマップ / GPU 駆動クアッドツリー）は
  M6 着手時に決める。
- レイヤー内部のマスク生成をノードグラフ化するかは、M4 の実装が固まってから判断する。
- リソース状態の管理単位。現在はリソース全体で 1 つしか持たない
  （ミップ連鎖の生成だけ例外的にサブリソース単位）。
  ミップごとに別状態へ遷移させたい場面が増えたら本格対応する。
- 自動露出と AgX トーンマップ。必要になった時点で追加する。
- パララックスとシャドウ。ハイトチャンネルが揃ったので実装可能だが、
  M4 のマスク生成を優先する。
