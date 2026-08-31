// レイヤー 1 枚ぶんを合成結果へ積む。
//
// 出力の 4 枚は UAV として読み書きする。各スレッドは自分のテクセルしか触らないので、
// 同一ディスパッチ内での読み書きは安全。レイヤー間は UAV バリアで区切る。
//
// 出力タイル矩形と解像度を引数に取る形は崩さないこと（エクスポート時のタイル評価に必要）。

#include "CompositeCommon.hlsli"

#define HM_SOURCE_CONSTANT 0
#define HM_SOURCE_NOISE    1
#define HM_SOURCE_TEXTURE  2
// 3..6 は合成の中間結果に由来するマスク。CompositeMask パスが事前に計算する。
#define HM_SOURCE_DERIVED  3
// ブラシで描いたマスク。PaintMaskStore が持つテクスチャをそのまま読む。
#define HM_SOURCE_PAINT    7

static const uint kInvalidTextureIndex = 0xFFFFFFFFu;

#define HM_FLAG_MASK_INVERT 0x1u
#define HM_FLAG_BASE_LAYER  0x2u

struct LayerConstants
{
    uint4 outputIndices;  // BaseColor, Normal, Surface, Height の UAV
    uint4 tile;           // x, y, width, height（出力全体の中での矩形）
    uint2 resolution;     // 出力全体の解像度
    uint channelMask;     // 書き込むチャンネルのビット
    uint flags;

    float4 baseColor;      // rgb
    float4 surfaceParams;  // roughness, metallic, ao, heightBase
    float4 blendParams;    // blendRange, normalStrength, uvScale, heightSource
    float4 maskParams;     // constant, levelsLow, levelsHigh, maskSource
    float4 heightNoise;    // scale, amount, octaves, offset
    float4 maskNoise;      // scale, amount, octaves, offset

    // 参照するテクスチャの SRV インデックス。kInvalidTextureIndex なら定数を使う。
    uint4 textureIndices0;  // baseColor, normal, roughness, metallic
    uint4 textureIndices1;  // ao, height, mask, 中間結果由来マスクの SRV

    float4 maskCurve;   // contrast, derivedScale, 未使用, 未使用
    uint4 noiseTypes;   // height, mask, 未使用, 未使用
    uint4 paintParams;  // ペイントマスクの SRV, 未使用 x3
};

ConstantBuffer<LayerConstants> g_layer : register(b1);

// コンピュートシェーダでは暗黙の LOD が使えないため、出力テクセル 1 つが張る
// UV 幅からミップレベルを求めて SampleLevel する。
float TextureLod(Texture2D<float4> texture, float uvPerOutputTexel)
{
    uint width = 0;
    uint height = 0;
    uint mipCount = 0;
    texture.GetDimensions(0, width, height, mipCount);

    const float texelsPerOutputTexel = max(float(width) * uvPerOutputTexel, 1.0f);
    return clamp(log2(texelsPerOutputTexel), 0.0f, float(max(mipCount, 1u) - 1u));
}

float4 SampleLayerTexture(uint index, float2 uv, float uvPerOutputTexel)
{
    Texture2D<float4> texture = ResourceDescriptorHeap[index];
    return texture.SampleLevel(g_samplerLinearWrap, uv, TextureLod(texture, uvPerOutputTexel));
}

float SampleLayerHeight(float2 uv, float uvPerOutputTexel)
{
    float height = g_layer.surfaceParams.w;

    const uint source = uint(g_layer.blendParams.w);
    if (source == HM_SOURCE_NOISE)
    {
        const float2 p = uv * g_layer.heightNoise.x + g_layer.heightNoise.w;
        height += SampleNoise(g_layer.noiseTypes.x, p, int(g_layer.heightNoise.z)) *
                  g_layer.heightNoise.y;
    }
    else if (source == HM_SOURCE_TEXTURE && g_layer.textureIndices1.y != kInvalidTextureIndex)
    {
        const float sampled =
            SampleLayerTexture(g_layer.textureIndices1.y, uv, uvPerOutputTexel).r;
        height += sampled * g_layer.heightNoise.y;
    }

    return height;
}

float SampleMaskSourceValue(float2 uv, float2 paintUv, uint2 texel, float uvPerOutputTexel)
{
    const uint source = uint(g_layer.maskParams.w);

    if (source == HM_SOURCE_PAINT)
    {
        if (g_layer.paintParams.x == kInvalidTextureIndex)
        {
            return g_layer.maskParams.x;
        }
        // ペイントマスクはレイヤーの UV スケールを掛けない出力そのものの座標で引く。
        // ブラシはメッシュ上で見えている位置に描くため、合成結果と 1 対 1 で対応する。
        Texture2D<float> paint = ResourceDescriptorHeap[g_layer.paintParams.x];
        return g_layer.maskParams.x *
               paint.SampleLevel(g_samplerLinearWrap, paintUv, 0.0f);
    }

    if (source == HM_SOURCE_NOISE)
    {
        const float2 p = uv * g_layer.maskNoise.x + g_layer.maskNoise.w;
        const float noise = SampleNoise(g_layer.noiseTypes.y, p, int(g_layer.maskNoise.z));
        // ノイズだけは加算。定数を基準に揺らす。
        return g_layer.maskParams.x + (noise - 0.5f) * g_layer.maskNoise.y;
    }

    if (source == HM_SOURCE_TEXTURE)
    {
        if (g_layer.textureIndices1.z == kInvalidTextureIndex)
        {
            return g_layer.maskParams.x;
        }
        return g_layer.maskParams.x *
               SampleLayerTexture(g_layer.textureIndices1.z, uv, uvPerOutputTexel).r;
    }

    if (source >= HM_SOURCE_DERIVED)
    {
        if (g_layer.textureIndices1.w == kInvalidTextureIndex)
        {
            return g_layer.maskParams.x;
        }
        Texture2D<float> derived = ResourceDescriptorHeap[g_layer.textureIndices1.w];
        return g_layer.maskParams.x * derived[texel];
    }

    return g_layer.maskParams.x;
}

float SampleLayerMask(float2 uv, float2 paintUv, uint2 texel, float uvPerOutputTexel)
{
    float mask = saturate(SampleMaskSourceValue(uv, paintUv, texel, uvPerOutputTexel));
    mask = ApplyMaskCurve(mask, g_layer.maskCurve.x);

    const bool invert = (g_layer.flags & HM_FLAG_MASK_INVERT) != 0u;
    return ApplyMaskLevels(mask, g_layer.maskParams.y, g_layer.maskParams.z, invert);
}

// 勾配（高さ / UV 単位）を法線の傾きへ変換する係数。
// 生の d(height)/d(uv) はノイズ周波数がそのまま出て極端に急になるため、
// 強さ 1.0 が妥当な見た目になるよう一定倍率で丸める。
static const float kNormalGradientScale = 0.03f;

// ハイトの勾配からタンジェント空間法線を作る。解像度に依らない値になるよう、
// テクセル差ではなく UV 単位の微分を取る。
// 法線テクスチャが指定されている場合はそちらを使う。
float3 ComputeLayerNormal(float2 uv, float2 texelSize, float uvPerOutputTexel)
{
    const float strength = g_layer.blendParams.y;

    if (g_layer.textureIndices0.y != kInvalidTextureIndex)
    {
        const float3 sampled =
            SampleLayerTexture(g_layer.textureIndices0.y, uv, uvPerOutputTexel).rgb;
        const float3 tangentNormal = normalize(sampled * 2.0f - 1.0f);
        return FlattenNormal(tangentNormal, strength);
    }

    if (strength <= 0.0f)
    {
        return float3(0.0f, 0.0f, 1.0f);
    }

    const float hx0 = SampleLayerHeight(uv - float2(texelSize.x, 0.0f), uvPerOutputTexel);
    const float hx1 = SampleLayerHeight(uv + float2(texelSize.x, 0.0f), uvPerOutputTexel);
    const float hy0 = SampleLayerHeight(uv - float2(0.0f, texelSize.y), uvPerOutputTexel);
    const float hy1 = SampleLayerHeight(uv + float2(0.0f, texelSize.y), uvPerOutputTexel);

    // テクセル間隔で正規化した勾配。強さで倍率を掛ける。
    const float dx = (hx1 - hx0) * 0.5f / max(texelSize.x, 1e-6f);
    const float dy = (hy1 - hy0) * 0.5f / max(texelSize.y, 1e-6f);

    const float scale = strength * kNormalGradientScale;
    return normalize(float3(-dx * scale, -dy * scale, 1.0f));
}

[numthreads(8, 8, 1)]
void CsMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x >= g_layer.tile.z || dispatchThreadId.y >= g_layer.tile.w)
    {
        return;
    }

    const uint2 texel = g_layer.tile.xy + dispatchThreadId.xy;

    RWTexture2D<float4> baseColorTarget = ResourceDescriptorHeap[g_layer.outputIndices.x];
    RWTexture2D<float2> normalTarget    = ResourceDescriptorHeap[g_layer.outputIndices.y];
    RWTexture2D<float4> surfaceTarget   = ResourceDescriptorHeap[g_layer.outputIndices.z];
    RWTexture2D<float>  heightTarget    = ResourceDescriptorHeap[g_layer.outputIndices.w];

    const float2 texelSize = 1.0f / float2(g_layer.resolution);
    // ペイントマスクは出力そのものの座標で引くため、UV スケールを掛ける前を残しておく。
    const float2 outputUv = (float2(texel) + 0.5f) * texelSize;
    const float2 uv = outputUv * g_layer.blendParams.z;
    const float2 noiseTexelSize = texelSize * g_layer.blendParams.z;

    // 出力テクセル 1 つが張る UV 幅。テクスチャのミップ選択に使う。
    const float uvPerOutputTexel = texelSize.x * g_layer.blendParams.z;

    // --- レイヤーの値 ------------------------------------------------------
    float3 layerBaseColor = g_layer.baseColor.rgb;
    float layerRoughness = g_layer.surfaceParams.x;
    float layerMetallic = g_layer.surfaceParams.y;
    float layerAo = g_layer.surfaceParams.z;

    if (g_layer.textureIndices0.x != kInvalidTextureIndex)
    {
        layerBaseColor *= SampleLayerTexture(g_layer.textureIndices0.x, uv, uvPerOutputTexel).rgb;
    }
    if (g_layer.textureIndices0.z != kInvalidTextureIndex)
    {
        layerRoughness = SampleLayerTexture(g_layer.textureIndices0.z, uv, uvPerOutputTexel).r;
    }
    if (g_layer.textureIndices0.w != kInvalidTextureIndex)
    {
        layerMetallic = SampleLayerTexture(g_layer.textureIndices0.w, uv, uvPerOutputTexel).r;
    }
    if (g_layer.textureIndices1.x != kInvalidTextureIndex)
    {
        layerAo = SampleLayerTexture(g_layer.textureIndices1.x, uv, uvPerOutputTexel).r;
    }

    const float layerHeight = SampleLayerHeight(uv, uvPerOutputTexel);
    const float3 layerNormal = ComputeLayerNormal(uv, noiseTexelSize, uvPerOutputTexel);

    const bool isBaseLayer = (g_layer.flags & HM_FLAG_BASE_LAYER) != 0u;

    float weight = 1.0f;
    if (!isBaseLayer)
    {
        const float mask = SampleLayerMask(uv, outputUv, texel, uvPerOutputTexel);
        const float destinationHeight = heightTarget[texel];
        weight = HeightBlendWeight(destinationHeight, layerHeight, mask, g_layer.blendParams.x);
    }

    // --- 各チャンネルへ積む ------------------------------------------------
    if ((g_layer.channelMask & 0x1u) != 0u)
    {
        const float3 destination = isBaseLayer ? layerBaseColor : baseColorTarget[texel].rgb;
        baseColorTarget[texel] = float4(lerp(destination, layerBaseColor, weight), 1.0f);
    }

    if ((g_layer.channelMask & 0x2u) != 0u)
    {
        float3 result;
        if (isBaseLayer)
        {
            result = layerNormal;
        }
        else
        {
            // 重みに応じて下地を平坦へ寄せ、レイヤー側も弱めてから RNM で合成する。
            // weight = 0 で下地、weight = 1 でレイヤーそのものになる。
            const float3 destination = DecodeTangentNormal(normalTarget[texel]);
            const float3 flattenedBase = FlattenNormal(destination, 1.0f - weight);
            const float3 attenuatedDetail = FlattenNormal(layerNormal, weight);
            result = ReorientNormal(flattenedBase, attenuatedDetail);
        }
        normalTarget[texel] = EncodeTangentNormal(result);
    }

    if ((g_layer.channelMask & 0x4u) != 0u)
    {
        const float3 layerSurface = float3(layerRoughness, layerMetallic, layerAo);
        const float3 destination = isBaseLayer ? layerSurface : surfaceTarget[texel].rgb;
        surfaceTarget[texel] = float4(lerp(destination, layerSurface, weight), 1.0f);
    }

    if ((g_layer.channelMask & 0x8u) != 0u)
    {
        const float destination = isBaseLayer ? layerHeight : heightTarget[texel];
        heightTarget[texel] = lerp(destination, layerHeight, weight);
    }
}
