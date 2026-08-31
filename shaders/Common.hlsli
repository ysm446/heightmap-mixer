#ifndef HM_COMMON_HLSLI
#define HM_COMMON_HLSLI

// 全パス共通のルートシグネチャに対応する宣言。
// b0 は各シェーダが自前のルート定数構造体を宣言するため、ここでは定義しない。
//
// リソースは bindless で引く。SRV / UAV はディスクリプタのインデックスを
// ルート定数で受け取り、ResourceDescriptorHeap[index] でアクセスする。

SamplerState g_samplerPointClamp  : register(s0);
SamplerState g_samplerLinearClamp : register(s1);
SamplerState g_samplerLinearWrap  : register(s2);
SamplerState g_samplerAnisoWrap   : register(s3);

static const float kPi = 3.14159265358979323846f;

float3 SrgbToLinear(float3 c)
{
    return select(c <= 0.04045f, c / 12.92f, pow(abs(c + 0.055f) / 1.055f, 2.4f));
}

float3 LinearToSrgb(float3 c)
{
    return select(c <= 0.0031308f, c * 12.92f, 1.055f * pow(abs(c), 1.0f / 2.4f) - 0.055f);
}

// 決定的なハッシュノイズ。マスク生成の足場として使う。
float Hash21(float2 p)
{
    float3 p3 = frac(float3(p.xyx) * 0.1031f);
    p3 += dot(p3, p3.yzx + 33.33f);
    return frac((p3.x + p3.y) * p3.z);
}

float ValueNoise(float2 p)
{
    float2 i = floor(p);
    float2 f = frac(p);
    float2 u = f * f * (3.0f - 2.0f * f);

    float a = Hash21(i + float2(0.0f, 0.0f));
    float b = Hash21(i + float2(1.0f, 0.0f));
    float c = Hash21(i + float2(0.0f, 1.0f));
    float d = Hash21(i + float2(1.0f, 1.0f));

    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}

float Fbm(float2 p, int octaves)
{
    float sum = 0.0f;
    float amplitude = 0.5f;
    for (int i = 0; i < octaves; ++i)
    {
        sum += ValueNoise(p) * amplitude;
        p *= 2.0f;
        amplitude *= 0.5f;
    }
    return sum;
}

#endif  // HM_COMMON_HLSLI
