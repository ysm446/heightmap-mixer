// マテリアルプレビューのメッシュ描画。
// 出力はトーンマップ前の線形放射輝度で、露出は後段の TonemapPass で掛ける。

#include "Brdf.hlsli"

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
    float3 pad2;
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
    float2 uv            : TEXCOORD0;
};

VsOutput VsMain(VsInput input)
{
    VsOutput output;

    const float4 worldPosition = mul(g_mesh.model, float4(input.position, 1.0f));
    output.worldPosition = worldPosition.xyz;
    output.clipPosition = mul(g_mesh.viewProjection, worldPosition);
    output.worldNormal = mul((float3x3)g_mesh.normalMatrix, input.normal);
    output.uv = input.uv;

    return output;
}

float4 PsMain(VsOutput input) : SV_Target
{
    const float3 normal = normalize(input.worldNormal);
    const float3 viewDirection = normalize(g_mesh.cameraPosition - input.worldPosition);

    float3 diffuseColor;
    float3 f0;
    SplitBaseColor(g_mesh.baseColor, g_mesh.metallic, diffuseColor, f0);

    const float roughness = clamp(g_mesh.roughness, 0.03f, 1.0f);

    float3 radiance = ShadeDirectionalLight(normal, viewDirection,
                                            normalize(g_mesh.lightDirection), g_mesh.lightColor,
                                            g_mesh.lightIlluminance, diffuseColor, f0, roughness);

    // IBL 導入までの繋ぎ。空と地面をおおまかに模した環境光を足しておく。
    // M2b で prefiltered environment に置き換える。
    const float upness = normal.y * 0.5f + 0.5f;
    const float3 skyColor = float3(0.30f, 0.42f, 0.60f);
    const float3 groundColor = float3(0.18f, 0.16f, 0.14f);
    const float3 ambient = lerp(groundColor, skyColor, upness) * g_mesh.lightIlluminance * 0.03f;
    radiance += diffuseColor * ambient * DiffuseLambert();

    return float4(radiance, 1.0f);
}
