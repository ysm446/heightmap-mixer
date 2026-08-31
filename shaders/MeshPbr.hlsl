// マテリアルプレビューのメッシュ描画。
// 出力はトーンマップ前の線形放射輝度で、露出は後段の TonemapPass で掛ける。

#include "Brdf.hlsli"
#include "CompositeCommon.hlsli"
#include "EnvCommon.hlsli"

struct MeshConstants
{
    float4x4 viewProjection;
    float4x4 model;
    float4x4 normalMatrix;

    float3 cameraPosition;
    float pad0;

    float3 lightDirection;   // サーフェスから光源へ向かう方向
    float lightIlluminance;  // lux 相当

    float3 lightColor;
    float pad1;

    float3 baseColor;
    float roughness;

    float metallic;
    float iblIntensity;
    uint prefilteredMipCount;
    float pad2;

    uint irradianceIndex;    // irradiance キューブの SRV
    uint prefilteredIndex;   // プリフィルタ済みキューブの SRV
    uint brdfLutIndex;       // 環境 BRDF の LUT
    uint useMaterialTextures;  // 0 なら UI の単色パラメータを使う

    // 合成結果のチャンネル（bindless）
    uint materialBaseColorIndex;
    uint materialNormalIndex;
    uint materialSurfaceIndex;
    uint materialHeightIndex;

    float materialUvScale;
    float3 pad4;
};

ConstantBuffer<MeshConstants> g_mesh : register(b1);

struct VsInput
{
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float4 tangent  : TANGENT;
    float2 uv       : TEXCOORD0;
};

struct VsOutput
{
    float4 clipPosition  : SV_Position;
    float3 worldPosition : WORLDPOSITION;
    float3 worldNormal   : NORMAL;
    float3 worldTangent  : TANGENT;
    float tangentSign    : TANGENTSIGN;
    float2 uv            : TEXCOORD0;
};

VsOutput VsMain(VsInput input)
{
    VsOutput output;

    const float4 worldPosition = mul(g_mesh.model, float4(input.position, 1.0f));
    output.worldPosition = worldPosition.xyz;
    output.clipPosition = mul(g_mesh.viewProjection, worldPosition);
    output.worldNormal = mul((float3x3)g_mesh.normalMatrix, input.normal);
    output.worldTangent = mul((float3x3)g_mesh.model, input.tangent.xyz);
    output.tangentSign = input.tangent.w;
    output.uv = input.uv;

    return output;
}

float4 PsMain(VsOutput input) : SV_Target
{
    const float3 geometricNormal = normalize(input.worldNormal);
    const float3 viewDirection = normalize(g_mesh.cameraPosition - input.worldPosition);

    float3 baseColor = g_mesh.baseColor;
    float roughnessValue = g_mesh.roughness;
    float metallicValue = g_mesh.metallic;
    float ambientOcclusion = 1.0f;
    float3 normal = geometricNormal;

    if (g_mesh.useMaterialTextures != 0u)
    {
        Texture2D<float4> baseColorMap = ResourceDescriptorHeap[g_mesh.materialBaseColorIndex];
        Texture2D<float2> normalMap    = ResourceDescriptorHeap[g_mesh.materialNormalIndex];
        Texture2D<float4> surfaceMap   = ResourceDescriptorHeap[g_mesh.materialSurfaceIndex];

        const float2 uv = input.uv * g_mesh.materialUvScale;

        baseColor = baseColorMap.Sample(g_samplerAnisoWrap, uv).rgb;

        const float3 surface = surfaceMap.Sample(g_samplerAnisoWrap, uv).rgb;
        roughnessValue = surface.r;
        metallicValue = surface.g;
        ambientOcclusion = surface.b;

        // タンジェント空間法線をワールド空間へ移す。
        const float3 tangentNormal = DecodeTangentNormal(normalMap.Sample(g_samplerAnisoWrap, uv));
        const float3 tangent =
            normalize(input.worldTangent - geometricNormal * dot(geometricNormal, input.worldTangent));
        const float3 bitangent = cross(geometricNormal, tangent) * input.tangentSign;
        normal = normalize(tangent * tangentNormal.x + bitangent * tangentNormal.y +
                           geometricNormal * tangentNormal.z);
    }

    float3 diffuseColor;
    float3 f0;
    SplitBaseColor(baseColor, metallicValue, diffuseColor, f0);

    const float roughness = clamp(roughnessValue, 0.03f, 1.0f);

    float3 radiance = ShadeDirectionalLight(normal, viewDirection,
                                            normalize(g_mesh.lightDirection), g_mesh.lightColor,
                                            g_mesh.lightIlluminance, diffuseColor, f0, roughness);

    // --- IBL（分割和近似） -------------------------------------------------
    const float nDotV = saturate(dot(normal, viewDirection)) + 1e-5f;

    TextureCube<float4> irradianceMap = ResourceDescriptorHeap[g_mesh.irradianceIndex];
    TextureCube<float4> prefilteredMap = ResourceDescriptorHeap[g_mesh.prefilteredIndex];
    Texture2D<float2> brdfLut = ResourceDescriptorHeap[g_mesh.brdfLutIndex];

    // irradiance マップには E / pi（平均放射輝度）が入っているので、
    // diffuseColor を掛けるだけでよい。
    const float3 irradiance = irradianceMap.SampleLevel(g_samplerLinearClamp, normal, 0.0f).rgb;

    const float3 fresnel = FresnelSchlickRoughness(f0, nDotV, roughness);
    const float3 kD = 1.0f - fresnel;
    const float3 diffuseIbl = kD * diffuseColor * irradiance;

    const float3 reflectionDirection = reflect(-viewDirection, normal);
    const float mipLevel = roughness * float(max(g_mesh.prefilteredMipCount, 1u) - 1u);
    const float3 prefiltered =
        prefilteredMap.SampleLevel(g_samplerLinearClamp, reflectionDirection, mipLevel).rgb;

    const float2 environmentBrdf =
        brdfLut.SampleLevel(g_samplerLinearClamp, float2(nDotV, roughness), 0.0f);
    const float3 specularIbl = prefiltered * (f0 * environmentBrdf.x + environmentBrdf.y);

    radiance += (diffuseIbl + specularIbl) * g_mesh.iblIntensity * ambientOcclusion;

    return float4(radiance, 1.0f);
}
