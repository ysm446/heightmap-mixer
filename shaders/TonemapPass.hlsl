// シーンカラー（線形 HDR）に露出を掛け、トーンマップして sRGB で書き出す。

#include "Tonemap.hlsli"

struct TonemapConstants
{
    uint sourceIndex;
    uint outputIndex;
    uint width;
    uint height;
    float exposure;
    uint tonemapMode;
};

ConstantBuffer<TonemapConstants> g_constants : register(b0);

[numthreads(8, 8, 1)]
void CsMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x >= g_constants.width || dispatchThreadId.y >= g_constants.height)
    {
        return;
    }

    Texture2D<float4> source = ResourceDescriptorHeap[g_constants.sourceIndex];
    RWTexture2D<float4> output = ResourceDescriptorHeap[g_constants.outputIndex];

    float3 color = source[dispatchThreadId.xy].rgb;
    color *= g_constants.exposure;
    color = ApplyTonemap(color, g_constants.tonemapMode);

    output[dispatchThreadId.xy] = float4(LinearToSrgb(color), 1.0f);
}
