// マテリアル一覧に出すサムネイルを描く。
//
// メッシュは使わず、正面を向いた球を解析的に解く。円の内側だけを塗り、
// 外側は背景で埋める。マップは円板の UV でそのまま引く。
// 見た目を比べるためのものなので、プレビュー本体と厳密に一致させる必要はない。

#include "Brdf.hlsli"
#include "CompositeCommon.hlsli"
#include "Tonemap.hlsli"

struct ThumbnailConstants
{
    uint outputIndex;
    uint size;
    uint baseColorIndex;   // sRGB の SRV。kInvalidTextureIndex なら定数
    uint normalIndex;

    uint roughnessIndex;
    uint metallicIndex;
    uint aoIndex;
    uint heightIndex;      // 今は使わない。将来の視差用に枠だけ確保する

    float3 baseColorTint;
    float roughnessValue;

    float metallicValue;
    float aoValue;
    float uvScale;
    // スカラーのマップのチャンネル指定。4bit ずつ MM_CHANNEL_SLOT_* の順。
    uint mapChannels;
};

ConstantBuffer<ThumbnailConstants> g_thumbnail : register(b0);

static const uint kInvalidTextureIndex = 0xFFFFFFFFu;

// 背景。UI のパネル面（#232323）に近い明るさへ落ち着かせる。
static const float3 kBackground = float3(0.055f, 0.055f, 0.055f);

float4 SampleMap(uint index, float2 uv)
{
    Texture2D<float4> map = ResourceDescriptorHeap[index];
    return map.SampleLevel(g_samplerLinearWrap, uv, 0.0f);
}

// スカラーのマップを 1 つ読む。指定されたチャンネルだけを取り出す。
float SampleScalarMap(uint index, uint channelSlot, float2 uv)
{
    return SelectChannel(SampleMap(index, uv),
                         UnpackChannel(g_thumbnail.mapChannels, channelSlot));
}

[numthreads(8, 8, 1)]
void CsMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x >= g_thumbnail.size || dispatchThreadId.y >= g_thumbnail.size)
    {
        return;
    }

    RWTexture2D<float4> output = ResourceDescriptorHeap[g_thumbnail.outputIndex];

    // 出力の中心を原点、半径 1 の円に正規化する。y は上向きにする。
    const float2 uvCentered =
        ((float2(dispatchThreadId.xy) + 0.5f) / float(g_thumbnail.size)) * 2.0f - 1.0f;
    const float2 disc = float2(uvCentered.x, -uvCentered.y);
    const float radiusSq = dot(disc, disc);

    // 少し余白を取って球を収める。
    const float sphereRadius = 0.92f;
    if (radiusSq > sphereRadius * sphereRadius)
    {
        output[dispatchThreadId.xy] = float4(LinearToSrgb(kBackground), 1.0f);
        return;
    }

    const float2 spherePoint = disc / sphereRadius;
    const float3 geometricNormal =
        float3(spherePoint, sqrt(saturate(1.0f - dot(spherePoint, spherePoint))));

    // マップは円板の座標でそのまま引く。球へ厳密に貼るのではなく、
    // 「その素材がどう見えるか」を確かめられれば足りる。
    const float2 uv = (spherePoint * 0.5f + 0.5f) * g_thumbnail.uvScale;

    float3 baseColor = g_thumbnail.baseColorTint;
    if (g_thumbnail.baseColorIndex != kInvalidTextureIndex)
    {
        baseColor *= SampleMap(g_thumbnail.baseColorIndex, uv).rgb;
    }

    float roughness = g_thumbnail.roughnessValue;
    if (g_thumbnail.roughnessIndex != kInvalidTextureIndex)
    {
        roughness = SampleScalarMap(g_thumbnail.roughnessIndex,
                                    MM_CHANNEL_SLOT_ROUGHNESS, uv);
    }

    float metallic = g_thumbnail.metallicValue;
    if (g_thumbnail.metallicIndex != kInvalidTextureIndex)
    {
        metallic = SampleScalarMap(g_thumbnail.metallicIndex, MM_CHANNEL_SLOT_METALLIC, uv);
    }

    float ambientOcclusion = g_thumbnail.aoValue;
    if (g_thumbnail.aoIndex != kInvalidTextureIndex)
    {
        ambientOcclusion = SampleScalarMap(g_thumbnail.aoIndex, MM_CHANNEL_SLOT_AO, uv);
    }

    // 球の接空間は、正面を向いているので x が接線、y が従法線でよい。
    float3 normal = geometricNormal;
    if (g_thumbnail.normalIndex != kInvalidTextureIndex)
    {
        const float3 sampled = SampleMap(g_thumbnail.normalIndex, uv).rgb * 2.0f - 1.0f;
        const float3 tangent =
            normalize(float3(1.0f, 0.0f, 0.0f) -
                      geometricNormal * geometricNormal.x);
        const float3 bitangent = cross(geometricNormal, tangent);
        normal = normalize(tangent * sampled.x + bitangent * sampled.y +
                           geometricNormal * sampled.z);
    }

    float3 diffuseColor;
    float3 f0;
    SplitBaseColor(baseColor, metallic, diffuseColor, f0);

    const float clampedRoughness = clamp(roughness, 0.05f, 1.0f);
    const float3 viewDirection = float3(0.0f, 0.0f, 1.0f);
    const float3 lightDirection = normalize(float3(-0.45f, 0.55f, 0.70f));

    // 一覧の中で明るさが揃うよう、露出は掛けずに正規化した強さで直接シェーディングする。
    float3 radiance = ShadeDirectionalLight(normal, viewDirection, lightDirection,
                                            float3(1.0f, 0.98f, 0.95f), 2.6f, diffuseColor, f0,
                                            clampedRoughness);

    // 環境光の代わり。上からの弱い半球光で、影側が真っ黒にならないようにする。
    const float hemisphere = saturate(normal.y * 0.5f + 0.5f);
    radiance += diffuseColor * lerp(0.06f, 0.24f, hemisphere) * ambientOcclusion;

    // 縁を少し落として球らしく見せる。
    const float edge = smoothstep(1.0f, 0.86f, sqrt(radiusSq) / sphereRadius);
    radiance = lerp(kBackground, radiance, edge);

    output[dispatchThreadId.xy] = float4(LinearToSrgb(ApplyTonemap(radiance, 2)), 1.0f);
}
