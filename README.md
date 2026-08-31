# heightmap-mixer

Quixel Mixer 相当のマテリアルオーサリングツール。
ハイトマップと PBR テクスチャをレイヤー合成してマテリアルを作り、それを地形に適用する。

- 言語: C++20
- ビルド: CMake + vcpkg（manifest モード）
- グラフィックス API: DirectX 12（シェーダモデル 6.6、bindless）
- UI: Dear ImGui（docking ブランチ）
- 対象 OS: Windows のみ

## いまできること

- **レイヤー合成**: レイヤーを積み重ね、**ハイトベースブレンド**で合成する。
  単純なアルファ合成ではなく高さの大小で上下関係が決まるため、
  「岩の隙間に砂が溜まる」といった表現が作れる。
- **チャンネル**: BaseColor / Normal / Surface（Roughness・Metallic・AO）/ Height。
  レイヤーごとに書き込むチャンネルを選べる。
- **レイヤーの入力**: 定数、フラクタルノイズ、テクスチャ。
  法線はハイトの勾配から自動生成される（法線テクスチャの指定も可）。
- **PBR プレビュー**: GGX 直接光 + IBL（分割和近似）。球 / 平面 / キューブ。
- **物理カメラの露出**: 絞り・シャッター・ISO から EV100 を求める方式と、EV 直接指定。
  トーンマップは ACES / Reinhard / なし。
- **環境**: 手続き的な空、または Radiance HDR (.hdr) の読み込み。背景表示。

未実装: マスク生成の拡充（傾斜 / 曲率 / AO 由来、ペイント）、プロジェクトの保存、
フル解像度エクスポート、地形。詳細は [docs/plan/plan.md](docs/plan/plan.md) を参照。

## 必要なもの

| 項目 | 確認済みの構成 |
| --- | --- |
| Visual Studio | 2026 Community (18.7) — MSVC 14.51 |
| Windows SDK | 10.0.26100.0 |
| CMake | 4.3 以上（`Visual Studio 18 2026` ジェネレータ対応版） |
| vcpkg | `C:/vcpkg` に導入済み |
| GPU | Resource Binding Tier 3 と シェーダモデル 6.6 に対応したもの |

vcpkg のパスは `VCPKG_ROOT` が設定されていればそれを、なければ `C:/vcpkg` を使う。
どちらも無い場合は構成時にエラーになる。

## ビルド

```powershell
cd D:\GitHub\heightmap-mixer
cmake --preset x64
cmake --build --preset x64-release
```

構成プリセットは `x64` の 1 つだけで、Debug / Release はビルドプリセットで切り替える
（Visual Studio ジェネレータのマルチコンフィグ構成のため）。

```powershell
cmake --build --preset x64-debug     # デバッグレイヤー + GPU ベースバリデーション付き
cmake --build --preset x64-release
```

初回は vcpkg が依存をビルドするため数分かかる。2 回目以降はキャッシュが効く。

## 実行

```powershell
$exe = "D:\GitHub\heightmap-mixer\build\bin\Release\heightmap_mixer.exe"
Start-Process -FilePath $exe -WorkingDirectory (Split-Path $exe)
```

作業ディレクトリを exe の場所にすること。UI のレイアウトが
`heightmap_mixer_imgui.ini` としてそこに保存される。

> **シェーダはソースツリーを直接参照する。**
> `shaders/` の絶対パスが構成時に実行ファイルへ焼き込まれるため、
> exe 単体を別マシンへ持っていっても動かない。
> 起動したままシェーダを編集して即反映できる開発の都合を優先している。
> 環境変数 `HM_SHADER_DIR` で参照先を上書きできる。

### コマンドライン

```
heightmap_mixer.exe [--hdri <path>] [--texture <path>]...
                    [--screenshot <path>] [--screenshot-frame <n>]
```

| オプション | 内容 |
| --- | --- |
| `--hdri <path>` | 起動時に Radiance HDR (.hdr) を環境マップとして読み込む |
| `--texture <path>` | 起動時にテクスチャライブラリへ読み込む（繰り返し指定可） |
| `--screenshot <path>` | 指定フレームまで描いてビューポートを PNG に書き出し、終了する |
| `--screenshot-frame <n>` | 書き出すフレーム番号（既定 8） |

`--screenshot` は画面キャプチャに頼らず描画結果を確認するための開発用オプション。
リモートデスクトップ経由や自動確認で使う。

## 操作

| 操作 | 動作 |
| --- | --- |
| ビューポートで左ドラッグ | カメラの回転 |
| ビューポートで中ボタン / 右ドラッグ | 注視点の平行移動 |
| ビューポートでホイール | ズーム |
| パネルのタイトルバーをドラッグ | パネルの移動 |

### パネル

- **レイヤー**（左）— レイヤーの追加 / 複製 / 削除 / 並べ替えと、選択中レイヤーの編集。
  一覧は上が最前面。
- **ビューポート**（中央）— 合成結果を貼ったプレビュー。
- **プレビュー設定**（右上）— 形状、合成結果の使用可否、UV スケール、合成解像度。
- **ライティングと露出**（右中）— ライト、露出、環境（IBL）、トーンマップ。
- **情報**（右下）— フレームレート、解像度、合成のレイヤー数とタイル数、PSO キャッシュ数。

### 合成の勘どころ

**マスクは不透明度として高さと同じ土俵で競合する。** 重みは
`a = 下地の高さ + (1 - マスク)`、`b = レイヤーの高さ + マスク` の比較で決まる。

- マスクを 1.0 にすると高さに関係なく全面を覆う。
- 双方のマスクが 0.5 のとき、高さの大小がそのまま勝敗になる。
  このとき上のレイヤーの**基準の高さ**が「溜まる水位」として働く。
- **境界の柔らかさ**を 0 に近づけると硬い置き換え、大きくすると高さの影響が薄れる。

既定のレイヤー構成は「岩の隙間に砂が溜まる」2 レイヤー。
砂レイヤーの「基準の高さ」を上下させると、砂と岩の境界がそのまま動く。

## ディレクトリ構成

```
CMakeLists.txt / CMakePresets.json / vcpkg.json
src/
  app/          アプリ本体、エントリポイント、UI パネル
  compositor/   レイヤースタックの定義、GPU 評価器、テクスチャライブラリ
  core/         ログ、Win32 ウィンドウ、画像入出力
  renderer/     PBR プレビュー描画、カメラ、メッシュ、IBL 環境
  rhi/          DX12 ラッパ（device, descriptor, PSO, resource, shader）
  ui/           Dear ImGui の統合
shaders/        HLSL（実行時に DXC でコンパイル、ホットリロード対応）
docs/
  plan/         goals / plan / progress — 進捗管理の入口
  design/       実装の設計ガイド
  reference/    設計資料、仕様メモ、調査資料
```

## ドキュメント

| ファイル | 内容 |
| --- | --- |
| [docs/plan/goals.md](docs/plan/goals.md) | プロジェクトの目的、完成形、重視する価値 |
| [docs/plan/plan.md](docs/plan/plan.md) | 技術選定、マイルストーン、判断を保留している点 |
| [docs/plan/progress.md](docs/plan/progress.md) | 進捗、完了済み作業、実装上の注意点 |
| [docs/design/rhi.md](docs/design/rhi.md) | bindless、ルートシグネチャ、リソース管理、シェーダ |
| [docs/design/rendering.md](docs/design/rendering.md) | 描画の流れ、露出とトーンマップ、IBL、行列の規約 |
| [docs/design/compositing.md](docs/design/compositing.md) | チャンネル定義、ハイトブレンド、RNM、タイル評価、テクスチャ |
| [docs/changelog.md](docs/changelog.md) | 変更履歴 |
| [AGENTS.md](AGENTS.md) / [CLAUDE.md](CLAUDE.md) | 作業ルール |
