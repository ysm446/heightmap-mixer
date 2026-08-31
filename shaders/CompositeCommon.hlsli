#ifndef HM_COMPOSITE_COMMON_HLSLI
#define HM_COMPOSITE_COMMON_HLSLI

#include "Common.hlsli"

// 合成の出力チャンネル。
//   BaseColor : R11G11B10_FLOAT   リニア色
//   Normal    : R16G16_FLOAT      タンジェント空間法線の xy（z は再構成）
//   Surface   : R8G8B8A8_UNORM    R=Roughness, G=Metallic, B=AO
//   Height    : R16_FLOAT         高さ（合成の駆動値かつ Displacement）

float3 DecodeTangentNormal(float2 xy)
{
    const float z = sqrt(saturate(1.0f - dot(xy, xy)));
    return float3(xy, z);
}

float2 EncodeTangentNormal(float3 normal)
{
    return normalize(normal).xy;
}

// Reoriented Normal Mapping。
// base に detail を載せた法線を返す。lerp は使わない。
//   detail が平坦 (0,0,1) なら base をそのまま返す
//   base が平坦 (0,0,1) なら detail をそのまま返す
float3 ReorientNormal(float3 base, float3 detail)
{
    const float3 t = base + float3(0.0f, 0.0f, 1.0f);
    const float3 u = detail * float3(-1.0f, -1.0f, 1.0f);
    return normalize(t * (dot(t, u) / max(t.z, 1e-5f)) - u);
}

// 法線を平坦方向へ寄せる。合成の重みに応じて base / detail を弱めるのに使う。
float3 FlattenNormal(float3 normal, float amount)
{
    return normalize(float3(normal.xy * amount, normal.z));
}

// ハイトベースブレンド。
//   下地の重み = 1 - mask、レイヤーの重み = mask として、
//   「高さ + 重み」の大きいほうが上に出る。
//   range を小さくすると硬い置き換え、大きくするとハイトの影響が薄れて
//   マスクによる従来の合成に近づく。
float HeightBlendWeight(float baseHeight, float layerHeight, float mask, float range)
{
    const float a = baseHeight + (1.0f - mask);
    const float b = layerHeight + mask;
    const float m = max(a, b) - max(range, 1e-4f);
    const float ca = max(a - m, 0.0f);
    const float cb = max(b - m, 0.0f);
    return saturate(cb / max(ca + cb, 1e-5f));
}

// マスクのレベル調整と反転。
float ApplyMaskLevels(float value, float low, float high, bool invert)
{
    const float range = max(high - low, 1e-4f);
    float result = saturate((value - low) / range);
    return invert ? (1.0f - result) : result;
}

#endif  // HM_COMPOSITE_COMMON_HLSLI
