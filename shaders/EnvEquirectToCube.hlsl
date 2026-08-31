// equirectangular マップをキューブマップへ変換する。

#include "EnvCommon.hlsli"

struct EquirectToCubeConstants
{
    uint sourceIndex;  // equirect の SRV
    uint outputIndex;  // キューブの UAV（Texture2DArray として書く）
    uint faceSize;
    // ファイルの値を cd/m^2 へ直す較正倍率。
    //
    // **HDRI ファイルは絶対輝度で較正されていない。** 画像内の比は本物だが、
    // 基準は入っていないので、ここで人が与える。手続き的な空は
    // すでに cd/m^2 で書き込んでいるため 1.0。
    float luminanceScale;
};

ConstantBuffer<EquirectToCubeConstants> g_constants : register(b0);

[numthreads(8, 8, 1)]
void CsMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x >= g_constants.faceSize || dispatchThreadId.y >= g_constants.faceSize)
    {
        return;
    }

    Texture2D<float4> source = ResourceDescriptorHeap[g_constants.sourceIndex];
    RWTexture2DArray<float4> output = ResourceDescriptorHeap[g_constants.outputIndex];

    const uint face = dispatchThreadId.z;
    const float3 direction = CubeFaceDirection(face, CubeFaceUv(dispatchThreadId.xy,
                                                               g_constants.faceSize));

    const float2 uv = DirectionToEquirectUv(direction);
    const float3 radiance = source.SampleLevel(g_samplerEquirect, uv, 0.0f).rgb;

    output[uint3(dispatchThreadId.xy, face)] = float4(radiance * g_constants.luminanceScale, 1.0f);
}
