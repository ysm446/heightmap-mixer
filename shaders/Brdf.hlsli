#ifndef HM_BRDF_HLSLI
#define HM_BRDF_HLSLI

#include "Common.hlsli"

// Cook-Torrance の各項。roughness は知覚的な値（perceptual roughness）で受け取り、
// 内部で alpha = roughness^2 に変換する。

float DistributionGGX(float nDotH, float roughness)
{
    const float alpha = roughness * roughness;
    const float alphaSq = alpha * alpha;
    const float denom = nDotH * nDotH * (alphaSq - 1.0f) + 1.0f;
    return alphaSq / max(kPi * denom * denom, 1e-7f);
}

// Smith の可視性項（G / (4 NoL NoV) を畳み込んだ形）。
float VisibilitySmithGgxCorrelated(float nDotV, float nDotL, float roughness)
{
    const float alpha = roughness * roughness;
    const float alphaSq = alpha * alpha;
    const float lambdaV = nDotL * sqrt(nDotV * nDotV * (1.0f - alphaSq) + alphaSq);
    const float lambdaL = nDotV * sqrt(nDotL * nDotL * (1.0f - alphaSq) + alphaSq);
    return 0.5f / max(lambdaV + lambdaL, 1e-7f);
}

float3 FresnelSchlick(float3 f0, float vDotH)
{
    const float f = pow(1.0f - vDotH, 5.0f);
    return f0 + (1.0f - f0) * f;
}

float3 FresnelSchlickRoughness(float3 f0, float nDotV, float roughness)
{
    const float3 fr = max(float3(1.0f - roughness, 1.0f - roughness, 1.0f - roughness), f0);
    return f0 + (fr - f0) * pow(1.0f - nDotV, 5.0f);
}

float DiffuseLambert()
{
    return 1.0f / kPi;
}

// 金属度から拡散色と F0 を求める。誘電体の反射率は 0.04 で固定する。
void SplitBaseColor(float3 baseColor, float metallic, out float3 diffuseColor, out float3 f0)
{
    diffuseColor = baseColor * (1.0f - metallic);
    f0 = lerp(float3(0.04f, 0.04f, 0.04f), baseColor, metallic);
}

// 1 灯ぶんの直接光。lightDirection は「サーフェスから光源へ向かう」正規化ベクトル。
// illuminance は光源の照度（lux 相当）。
float3 ShadeDirectionalLight(float3 normal, float3 viewDirection, float3 lightDirection,
                             float3 lightColor, float illuminance, float3 diffuseColor,
                             float3 f0, float roughness)
{
    const float nDotL = saturate(dot(normal, lightDirection));
    if (nDotL <= 0.0f)
    {
        return float3(0.0f, 0.0f, 0.0f);
    }

    const float3 halfVector = normalize(viewDirection + lightDirection);
    const float nDotV = saturate(dot(normal, viewDirection)) + 1e-5f;
    const float nDotH = saturate(dot(normal, halfVector));
    const float vDotH = saturate(dot(viewDirection, halfVector));

    const float d = DistributionGGX(nDotH, roughness);
    const float vis = VisibilitySmithGgxCorrelated(nDotV, nDotL, roughness);
    const float3 f = FresnelSchlick(f0, vDotH);

    const float3 specular = d * vis * f;
    const float3 diffuse = diffuseColor * DiffuseLambert() * (1.0f - f);

    return (diffuse + specular) * lightColor * illuminance * nDotL;
}

#endif  // HM_BRDF_HLSLI
