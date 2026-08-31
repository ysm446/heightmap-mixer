#pragma once

#include <DirectXMath.h>

#include <cstdint>
#include <string>

namespace hm::compositor {

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

// レイヤーの値の出どころ。
enum class ValueSource : uint32_t {
    Constant = 0,
    Noise = 1,
    Texture = 2,
};

// テクスチャの ID。0 は「なし」。TextureLibrary が払い出す。
using TextureId = uint32_t;
inline constexpr TextureId kNoTexture = 0;

// レイヤーが参照するテクスチャ。0 のスロットは定数値を使う。
struct LayerTextures {
    TextureId baseColor = kNoTexture;         // sRGB として読む
    TextureId normal = kNoTexture;            // タンジェント空間法線（リニア）
    TextureId roughness = kNoTexture;         // R チャンネル
    TextureId metallic = kNoTexture;          // R チャンネル
    TextureId ambientOcclusion = kNoTexture;  // R チャンネル
    TextureId height = kNoTexture;            // R チャンネル
    TextureId mask = kNoTexture;              // R チャンネル
};

// フラクタルノイズのパラメータ。ハイトとマスクで共通に使う。
struct NoiseParams {
    float scale = 6.0f;    // UV に掛ける周波数
    float amount = 1.0f;   // 出力への寄与
    int octaves = 5;
    float offset = 0.0f;   // 同じレイヤー内で別パターンにしたいときにずらす
};

// マスク。定数を基準に、ノイズで揺らし、レベル調整と反転を掛ける。
struct LayerMask {
    ValueSource source = ValueSource::Constant;
    float constant = 1.0f;
    NoiseParams noise{4.0f, 1.0f, 4, 37.0f};
    float levelsLow = 0.0f;
    float levelsHigh = 1.0f;
    bool invert = false;
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
    NoiseParams heightNoise{6.0f, 1.0f, 5, 0.0f};

    // 法線はハイトの勾配から作る。強さ 0 で平坦。
    float normalStrength = 1.0f;

    LayerMask mask;

    LayerTextures textures;

    // ハイトブレンドの境界の柔らかさ。0 に近いほど硬い置き換えになる。
    float blendRange = 0.15f;

    // このレイヤーの UV スケール。
    float uvScale = 1.0f;
};

}  // namespace hm::compositor
