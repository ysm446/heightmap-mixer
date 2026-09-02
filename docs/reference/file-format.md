# file-format — プロジェクトとマテリアルのファイル形式

作成日時: 2026-08-31 15:12
更新日時: 2026-09-02 12:50

実装は [src/io/ProjectIo.cpp](../../src/io/ProjectIo.cpp)。**形式を変えたらこの文書も直す。**

## 全体像

| 拡張子 | 内容 | 用途 |
| --- | --- | --- |
| `.mmproj` | プロジェクト全体（レイヤー / マテリアル / テクスチャの参照 / プレビュー設定） | 作業の保存と再開 |
| `.mmmat` | マテリアル 1 つ | プロジェクト間で持ち回る（書き出し / 読み込み） |
| `<名前>.assets/paint_NNNN.png` | ペイントマスク | `.mmproj` のサイドカー |

どちらも **UTF-8 の JSON**。人が読める形（インデント 2）で書き、差分も取れる。

### 3 つの原則

1. **プロジェクトはマテリアルの構造を丸ごと持つ。**
   `.mmproj` を開くのに `.mmmat` は要らない。`.mmmat` はあくまで持ち出し用で、
   プロジェクトが外部のマテリアルファイルに依存することはない。
2. **画像は参照で持つ。** テクスチャの中身はコピーせず、パスだけを記録する。
   パスは**そのファイルからの相対**で書き、プロジェクトごと移動しても壊れないようにする
   （ドライブが違うなど相対にできないときだけ絶対パス）。
3. **手続きで再現できないものだけ画像にする。** ペイントマスクがこれにあたるので、
   サイドカーのフォルダへ 8bit グレースケール PNG で書き出す。

### 共通のヘッダ

```json
{ "format": "material-mixer.project", "version": 3, "app": "0.1.0" }
```

- `format` — `material-mixer.project` または `material-mixer.material`。違えば読み込みを断る。
- `version` — 形式の版。**読み込み側は「ファイルの版 <= 対応版」なら読む。**
  未知のキーは無視し、欠けているキーは既定値（構造体の初期値）で埋める。
- `app` — 書き出したアプリのバージョン。参考情報で、読み込みでは見ない。

### 版の履歴

| 版 | 変更 |
| --- | --- |
| 1 | 最初の形式 |
| 2 | レイヤーのハイトに `gain` を追加し、`base` の意味を変えた（下記） |
| 3 | レイヤーに `kind`（種類）を追加した（下記） |

**版を上げる基準は「キーが増えたか」ではなく「既存のキーの意味が変わったか」。**
キーが増えただけなら、古いビルドはそれを無視して正しく読める。意味が変わった場合は、
古いビルドが黙って違う結果を出すので、断れるように版を上げる。

#### 版 2 — ハイトの基準面

版 1 のハイトは `h = base + src * amount` で、`amount` はノイズのパラメータが兼ねていた。
版 2 では基準面 0.5 を挟む `h = base + (src - 0.5) * gain` になり、`gain` が独立した。
理由は [design/compositing.md](../design/compositing.md) を参照。

読み込み時、`gain` が**無ければ版 1 と判断**して次のように移行する。

```
gain  = noise.amount
base' = base + 0.5 * gain     ただしソースが constant のときは base のまま
```

`base + src * gain == base' + (src - 0.5) * gain` なので、**近似ではなく厳密に同じ値**になる。
定数はそもそも `src` の項が無いため、触ると逆にずれる。

版ではなくキーの有無で判定しているのは、版が上がっても移行処理が正しく動くようにするため。

#### 版 3 — レイヤーの種類

レイヤーに `kind`（`surface` / `shape` / `liquid`）が付き、
**高さの合成規則が種類ごとに変わる**（[design/compositing.md](../design/compositing.md) の
「レイヤーの種類」）。`shape` と `liquid` では `height.base` / `blendRange` の
解釈も変わる（持ち上げ / 水位、フェザー）。古いビルドはキーを無視して
全レイヤーをサーフェスとして合成し、黙って違う絵を出すため版を上げた。

`kind` の無い旧ファイルは全レイヤーを `surface` として読む。これは移行ではなく
そのままの意味（版 2 以前にはサーフェスしか無い）。

あわせて `height.texture`（レイヤー直結のハイトマップ。スカラーのマップと同じ
「テクスチャ + チャンネル」の組）と `wrapToUnderlying`（下地に沿わせる。
サーフェスのコーティング）が増えた。どちらもキーの追加だけなので版は分けない。

## `.mmproj`

```
{
  format / version / app
  textures[]      画像への参照。id は 1 から振り直した通し番号
  materials[]     マテリアルの中身。テクスチャは textures の id で参照する
  paintMasks[]    ペイントマスク。実体はサイドカーの PNG
  paintResolution ペイントマスクの解像度（全マスク共通）
  layers[]        下から上へ。index 0 が一番下（下地）
  skies[]         天球。ビューポートの環境
  activeSky       ビューポートに適用している天球（skies の添字）
  preview{}       カメラ / ライト / 露出 / 被写界深度 / トーンマップ / 形状など
}
```

### 天球

```json
"skies": [
  {
    "name": "夕焼け",
    "source": "hdri",              // procedural / hdri
    "hdri": "../../data/hdr/pink_sunrise_4k.hdr",   // 使わなければ null
    "skyLuminance": 12000.0,       // この HDRI の空を何 cd/m^2 とみなすか
    "iblIntensity": 1.0,
    "procedural": { "zenithColor": [...], "horizonColor": [...],
                    "groundColor": [...], "intensity": 12000.0 }
  }
]
```

`skyLuminance` は**天球ごとに持つ**。HDRI は絶対輝度で較正されていないため
基準を外から与える必要があり、その値はファイルごとに違う。

`procedural` は `source` が `hdri` のときも書く。ソースを切り替えたときに
手で入れ直さなくて済むようにするため。

**`skies` が無いプロジェクトは、`preview` の `hdri` / `hdriSkyLuminance` /
`iblIntensity` / `sky` から天球を 1 つ作って読み込む**（天球を入れる前の形式）。
新しく保存すると `skies` の形に移る。

**id は保存のたびに 1 から振り直す。** 実行中の ID をそのまま書くと、
削除して番号が飛んだファイルになり読みにくい。読み込み側は
ファイル内の id → 実行時の ID の対応表を作って解決する。

参照できないものは `null` で表す（`"material": null` は「マテリアルなし」）。

```json
"textures": [
  { "id": 1, "name": "T_Gravel_D.png", "path": "../../data/textures/T_Gravel_D.png" }
]
```

`name` は一覧に出す表示名で、変えてもファイル名は変わらない。

### マップの参照

RGB をそのまま使うマップ（ベースカラー / 法線）はテクスチャ参照を直に書く。
スカラーのマップは「テクスチャ + 読むチャンネル」の組で書く
（Megascans の `_ORD` のように 1 枚へ詰めたテクスチャを使うため）。

```json
"maps": {
  "baseColor": 1,
  "normal": 2,
  "roughness":         { "texture": 3, "channel": "g" },
  "metallic":          { "texture": null, "channel": "r" },
  "ambientOcclusion":  { "texture": 3, "channel": "r" },
  "height":            { "texture": 3, "channel": "b" }
}
```

### ペイントマスク

```json
"paintMasks": [ { "id": 1, "resolution": 1024, "file": "paint_0001.png" } ]
```

`file` はサイドカーのフォルダ `<プロジェクト名>.assets/` からの相対名。
プロジェクト名から場所が決まるので、フォルダのパスは記録しない。

- 保存のたびに `paint_*.png` を書き直す。**消すのは自分が書いた名前だけ**で、
  同じフォルダの他のファイルには触らない。
- 書き出すのは**レイヤーが参照しているマスクだけ**。どのレイヤーからも
  参照されていないマスクは、次に開いたとき出てこない。

### 列挙は名前で書く

数値ではなく名前で書く。ファイルを直接読んだときに意味が分かるようにするため。

| 種別 | 値 |
| --- | --- |
| チャンネル指定 | `r` / `g` / `b` / `a` |
| レイヤーの種類 | `surface` / `shape` / `liquid` |
| ハイトのソース | `constant` / `noise` / `texture` |
| ノイズ | `fbm` / `ridged` / `worley` |
| マスクのソース | `constant` / `noise` / `texture` / `height` / `slope` / `curvature` / `cavity` / `paint` |
| 書き込むチャンネル | `["baseColor", "normal", "surface", "height"]`（配列） |
| プレビュー形状 | `sphere` / `plane` / `cube` |
| トーンマップ | `none` / `reinhard` / `aces` |

知らない名前が来たら既定値に落とす。

## `.mmmat`

マテリアル 1 つぶん。中身は `.mmproj` の `materials[]` の要素と同じで、
**テクスチャ参照だけが id ではなく相対パス**になる。

```json
{
  "format": "material-mixer.material",
  "version": 3,
  "name": "砂利",
  "baseColorTint": [1.0, 1.0, 1.0],
  "roughness": 0.5, "metallic": 0.0, "ambientOcclusion": 1.0,
  "maps": {
    "baseColor": "../../data/textures/T_Gravel_D.png",
    "normal":    "../../data/textures/T_Gravel_N.png",
    "roughness": { "texture": "../../data/textures/T_Gravel_ORD.png", "channel": "g" }
  }
}
```

読み込むと、参照している画像をテクスチャライブラリへ読み込み、
マテリアルを 1 つ**追加**する（既存のマテリアルには触らない）。
**同じパスの画像はすでに読み込んでいれば読み直さない。**
`_ORD` のように複数のマップが同じファイルを指していても 1 枚で済む。

## 壊れたファイルの扱い

例外は使わないので、失敗はすべて戻り値とログで表す。

- JSON として読めない / `format` が違う / 版が新しすぎる → **何も変更せずに断る。**
- 画像が見つからない → **警告を出して読み込みを続ける。**
  そのスロットは「なし」になる。プロジェクト全体を捨てない。
- 値の型が食い違っている → その項目だけ既定値に落とす。
- レイヤーが 1 枚も無い → 下地を 1 枚だけ置く（空のスタックは操作の起点が無い）。
