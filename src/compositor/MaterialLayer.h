#pragma once

#include <DirectXMath.h>

#include <cstdint>
#include <string>

namespace mm::compositor {

// 合成対象のチャンネル。出力テクスチャの構成と対応する。
enum class Channel : uint32_t {
    BaseColor = 0,
    Normal = 1,
    Surface = 2,  // R: Roughness, G: Metallic, B: AO
    Height = 3,
    Count = 4,
};

inline constexpr uint32_t ChannelBit(Channel channel) {
    return 1u << static_cast<uint32_t>(channel);
}

inline constexpr uint32_t kAllChannelBits = 0xFu;

// レイヤーの値のソース。
enum class ValueSource : uint32_t {
    Constant = 0,
    Noise = 1,
    Texture = 2,
};

// テクスチャの ID。0 は「なし」。TextureLibrary が払い出す。
using TextureId = uint32_t;
inline constexpr TextureId kNoTexture = 0;

// ペイントマスクの ID。0 は「なし」。PaintMaskStore が払い出す。
using PaintMaskId = uint32_t;
inline constexpr PaintMaskId kNoPaintMask = 0;

// マテリアルの ID。0 は「なし」。MaterialLibrary が払い出す。
using MaterialAssetId = uint32_t;
inline constexpr MaterialAssetId kNoMaterialAsset = 0;

// シェーダへ渡す「参照しない」を表すインデックス。
inline constexpr uint32_t kInvalidTextureIndex = 0xFFFFFFFFu;

// テクスチャのどのチャンネルを読むか。シェーダの SelectChannel と一致させること。
//
// Megascans の `_ORD` のように、1 枚のテクスチャへ複数のマップを詰めたものがある
// （O = Occlusion / R = Roughness / D = Displacement）。
// スカラーのマップはどれも「テクスチャ + チャンネル」で指定する。
enum class TextureChannel : uint32_t {
    R = 0,
    G = 1,
    B = 2,
    A = 3,
};

// スカラーのマップ 1 つぶん。
struct MapSlot {
    TextureId texture = kNoTexture;
    TextureChannel channel = TextureChannel::R;
};

// チャンネル指定をまとめてシェーダへ渡すための詰め方。4bit ずつ、最大 8 スロット。
// 並びはシェーダの MM_CHANNEL_* と一致させること。
inline constexpr uint32_t PackChannel(TextureChannel channel, uint32_t slotIndex) {
    return static_cast<uint32_t>(channel) << (slotIndex * 4u);
}

// ノイズの種類。シェーダの MM_NOISE_* と一致させること。
enum class NoiseType : uint32_t {
    Fbm = 0,     // 一般的なフラクタルノイズ
    Ridged = 1,  // 尾根状。稜線や割れ目に向く
    Worley = 2,  // セル状。石畳や砂利に向く
};

// フラクタルノイズのパラメータ。ハイトとマスクで共通に使う。
struct NoiseParams {
    NoiseType type = NoiseType::Fbm;
    float scale = 6.0f;    // UV に掛ける周波数
    float amount = 1.0f;   // 出力への寄与
    int octaves = 5;
    float offset = 0.0f;   // 同じレイヤー内で別パターンにしたいときにずらす
};

// マスクのソース。合成の中間結果に由来するものを含む。
//
// 「下地」とは、このレイヤーより下のレイヤーを合成した結果のこと。
// 傾斜や曲率を使うと「急斜面にだけ岩を出す」「窪みにだけ苔を生やす」が書ける。
enum class MaskSource : uint32_t {
    Constant = 0,
    Noise = 1,
    Texture = 2,
    Height = 3,     // 下地の高さ
    Slope = 4,      // 下地の傾斜（0 = 平坦、1 = 急）
    Curvature = 5,  // 下地の曲率（0.5 = 平坦、> 0.5 = 凸、< 0.5 = 凹）
    Cavity = 6,     // 下地の窪み（簡易 AO。1 に近いほど窪んでいる）
    Paint = 7,      // ブラシで描いたマスク（PaintMaskStore が持つテクスチャ）
};

// 中間結果由来かどうか。真なら評価前にマスク生成パスが要る。
inline bool IsDerivedMaskSource(MaskSource source) {
    return source == MaskSource::Height || source == MaskSource::Slope ||
           source == MaskSource::Curvature || source == MaskSource::Cavity;
}

// マスク。ソースの値に定数を掛け、カーブ・レベル調整・反転を掛ける。
struct LayerMask {
    MaskSource source = MaskSource::Constant;
    float constant = 1.0f;
    NoiseParams noise{NoiseType::Fbm, 4.0f, 1.0f, 4, 37.0f};
    // 中間結果由来のマスクの強調度。傾斜や曲率の効き方を調整する。
    float derivedScale = 1.0f;
    // カーブ。1 で線形、> 1 で中間を締める（コントラストが上がる）。
    float contrast = 1.0f;
    float levelsLow = 0.0f;
    float levelsHigh = 1.0f;
    bool invert = false;
    // ペイントマスク。source が Paint のときだけ参照する。
    PaintMaskId paint = kNoPaintMask;
    // マスク用テクスチャ。source が Texture のときだけ参照する。
    // マテリアルのマップとは用途が別なので、レイヤー側で持つ。
    MapSlot texture;
};

// 1 レイヤーぶんの設定。
struct MaterialLayer {
    std::string name = "レイヤー";
    bool enabled = true;

    // このレイヤーが書き込むチャンネル（Mixer と同じくチャンネル単位で切り替えられる）
    uint32_t channelMask = kAllChannelBits;

    DirectX::XMFLOAT3 baseColor = {0.5f, 0.5f, 0.5f};
    float roughness = 0.5f;
    float metallic = 0.0f;
    float ambientOcclusion = 1.0f;

    // ハイト。定数を基準に、ノイズを amount ぶん足す。
    ValueSource heightSource = ValueSource::Noise;
    float heightBase = 0.0f;
    NoiseParams heightNoise{NoiseType::Fbm, 6.0f, 1.0f, 5, 0.0f};

    // 法線はハイトの勾配から作る。強さ 0 で平坦。
    float normalStrength = 1.0f;

    LayerMask mask;

    // このレイヤーが使うマテリアル（PBR のマップ一式）。
    // kNoMaterialAsset なら下の定数値だけで塗る。
    MaterialAssetId material = kNoMaterialAsset;

    // ハイトブレンドの境界の柔らかさ。0 に近いほど硬い置き換えになる。
    float blendRange = 0.15f;

    // このレイヤーの UV スケール。
    float uvScale = 1.0f;
};

}  // namespace mm::compositor
