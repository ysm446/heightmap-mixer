// M1 の疎通確認用コンピュートシェーダ。
// DXC でのコンパイル、PSO キャッシュ、bindless の UAV 書き込み、
// リソース状態遷移が一通り動いていることを確かめる。

#include "Common.hlsli"

struct SmokeTestConstants
{
    uint outputIndex;   // ResourceDescriptorHeap 上の UAV インデックス
    uint width;
    uint height;
    float time;
};

ConstantBuffer<SmokeTestConstants> g_constants : register(b0);

[numthreads(8, 8, 1)]
void CsMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x >= g_constants.width || dispatchThreadId.y >= g_constants.height)
    {
        return;
    }

    RWTexture2D<float4> output = ResourceDescriptorHeap[g_constants.outputIndex];

    const float2 uv = (dispatchThreadId.xy + 0.5f) / float2(g_constants.width, g_constants.height);

    // ハイトマップらしさの確認を兼ねて fBm を出す。
    const float height = Fbm(uv * 8.0f + g_constants.time * 0.05f, 6);

    // 傾斜を数値微分で求め、合成マスクの足場が機能するかも見る。
    const float2 texel = 1.0f / float2(g_constants.width, g_constants.height);
    const float hx = Fbm((uv + float2(texel.x, 0.0f)) * 8.0f + g_constants.time * 0.05f, 6);
    const float hy = Fbm((uv + float2(0.0f, texel.y)) * 8.0f + g_constants.time * 0.05f, 6);
    const float slope = saturate(length(float2(hx - height, hy - height)) * 40.0f);

    const float3 rock = float3(0.32f, 0.30f, 0.28f);
    const float3 sand = float3(0.68f, 0.58f, 0.40f);
    const float3 color = lerp(sand, rock, slope) * (0.35f + 0.65f * height);

    output[dispatchThreadId.xy] = float4(color, 1.0f);
}
