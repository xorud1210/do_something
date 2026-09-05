#ifndef _LIGHTING_FX_
#define _LIGHTING_FX_

#include "params.fx"
#include "utils.fx"

struct VS_IN
{
    float3 pos : POSITION;
    float2 uv : TEXCOORD;
};

struct VS_OUT
{
    float4 pos : SV_Position;
    float2 uv : TEXCOORD;
};

struct PS_OUT
{
    float4 diffuse : SV_Target0;
    float4 specular : SV_Target1;
};

// [Directional Light]
// g_int_0  : Light index
// g_int_1  : 캐스케이드 개수
// g_int_2  : 캐스케이드 시각화 (F1)
// g_tex_0  : Position RT (뷰 공간)
// g_tex_1  : Normal RT (뷰 공간)
// g_tex_2  : Shadow 아틀라스 (4096 을 2x2 로 나눠 씀)
// g_mat_0~2: 캐스케이드별 라이트 ViewProjection
// g_vec4_0 : 캐스케이드 경계 (뷰 공간 깊이)
// g_vec4_1 : 캐스케이드별 텍셀 하나의 월드 크기
// g_float_0: 깊이 바이어스
// g_float_1: 아틀라스 텍셀 크기 (1 / 4096)
// Mesh : Rectangle

VS_OUT VS_DirLight(VS_IN input)
{
    VS_OUT output = (VS_OUT)0;

    output.pos = float4(input.pos * 2.f, 1.f);
    output.uv = input.uv;

    return output;
}

// 뷰 깊이로 어느 캐스케이드를 쓸지 고른다.
int SelectCascade(float viewDepth)
{
    int cascade = 0;

    [unroll]
    for (int i = 0; i < 2; ++i)
    {
        if (i < g_int_1 - 1 && viewDepth > g_vec4_0[i])
            cascade = i + 1;
    }

    return cascade;
}

matrix GetCascadeMatrix(int cascade)
{
    if (cascade == 0) return g_mat_0;
    if (cascade == 1) return g_mat_1;
    return g_mat_2;
}

// 아틀라스에서 이 캐스케이드가 차지하는 타일의 좌상단 (0~1 기준)
float2 GetCascadeTileOffset(int cascade)
{
    return float2((cascade % 2) * 0.5f, (cascade / 2) * 0.5f);
}

// 그림자 안이면 0, 밖이면 1.
// 한 번만 비교하면 셰도우 맵 텍셀 하나가 그대로 화면의 계단이 된다.
// 주변을 여러 번 비교해 평균 내면 그 경계가 텍셀 사이로 흩어진다 (PCF).
float SampleShadow(float3 worldPos, float3 worldNormal, float viewDepth)
{
    int cascade = SelectCascade(viewDepth);

    // 노멀 오프셋.
    // 표면에 비스듬히 닿는 빛일수록 셰도우 맵 텍셀 하나가 월드에서 길게 늘어나
    // 자기 자신을 가리는 줄무늬(셰도우 애크니)가 생긴다.
    // 표본 위치를 법선 쪽으로 텍셀 몇 개만큼 밀어내면 그 자기 교차가 사라진다.
    // 깊이 바이어스만으로 잡으려 하면 그림자가 물체에서 떨어져 뜬다(피터 패닝).
    float texelWorld = g_vec4_1[cascade];
    float3 offsetPos = worldPos + worldNormal * texelWorld * 1.5f;

    float4 shadowClip = mul(float4(offsetPos, 1.f), GetCascadeMatrix(cascade));
    float3 ndc = shadowClip.xyz / shadowClip.w;

    // 캐스케이드 밖으로 나가면 그림자를 만들지 않는다.
    if (any(abs(ndc.xy) > 1.f) || ndc.z > 1.f)
        return 1.f;

    float2 uv = ndc.xy * float2(0.5f, -0.5f) + 0.5f;
    uv = uv * 0.5f + GetCascadeTileOffset(cascade);   // 아틀라스 타일로 옮긴다

    float depth = ndc.z - g_float_0;
    float texel = g_float_1;

    // 3x3 PCF.
    // 점 샘플러(g_sam_2)로 읽는다. 선형 필터링으로 깊이를 섞은 뒤 비교하면
    // "섞인 깊이"와 비교하는 셈이라 결과가 무의미하다.
    // 부드럽게 만드는 것은 비교 결과 쪽을 평균 내서 한다.
    float shadow = 0.f;

    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            float2 tapUV = uv + float2(x, y) * texel;
            float mapDepth = g_tex_2.SampleLevel(g_sam_2, tapUV, 0).r;
            shadow += (depth > mapDepth) ? 0.f : 1.f;
        }
    }

    return shadow / 9.f;
}

PS_OUT PS_DirLight(VS_OUT input)
{
    PS_OUT output = (PS_OUT)0;

    float3 viewPos = g_tex_0.Sample(g_sam_0, input.uv).xyz;
    if (viewPos.z <= 0.f)
        clip(-1);

    float3 viewNormal = g_tex_1.Sample(g_sam_0, input.uv).xyz;

    LightColor color = CalculateLightColor(g_int_0, viewNormal, viewPos);

    if (length(color.diffuse) != 0)
    {
        float3 worldPos = mul(float4(viewPos, 1.f), g_matViewInv).xyz;
        float3 worldNormal = normalize(mul(float4(viewNormal, 0.f), g_matViewInv).xyz);

        float shadow = SampleShadow(worldPos, worldNormal, viewPos.z);

        // 그림자는 직접광을 가리는 것이지 환경광까지 없애는 게 아니다.
        // 그래서 ambient 는 건드리지 않고 diffuse 와 specular 만 줄인다.
        color.diffuse *= shadow;
        color.specular *= shadow;

        // 캐스케이드 시각화. 어느 구간이 어디서 갈리는지 눈으로 본다.
        if (g_int_2 == 1)
        {
            const float3 tint[3] =
            {
                float3(1.0f, 0.35f, 0.35f),
                float3(0.35f, 1.0f, 0.35f),
                float3(0.35f, 0.5f, 1.0f)
            };
            color.diffuse.rgb *= tint[SelectCascade(viewPos.z)];
        }
    }

    output.diffuse = color.diffuse + color.ambient;
    output.specular = color.specular;

    return output;
}

// [Point Light]
// g_int_0 : Light index
// g_tex_0 : Position RT
// g_tex_1 : Normal RT
// g_vec2_0 : RenderTarget Resolution
// Mesh : Sphere

VS_OUT VS_PointLight(VS_IN input)
{
    VS_OUT output = (VS_OUT)0;

    output.pos = mul(float4(input.pos, 1.f), g_matWVP);
    output.uv = input.uv;

    return output;
}

PS_OUT PS_PointLight(VS_OUT input)
{
    PS_OUT output = (PS_OUT)0;

    // input.pos = SV_Position = Screen 좌표
    float2 uv = float2(input.pos.x / g_vec2_0.x, input.pos.y / g_vec2_0.y);
    float3 viewPos = g_tex_0.Sample(g_sam_0, uv).xyz;
    if (viewPos.z <= 0.f)
        clip(-1);

    int lightIndex = g_int_0;
    float3 viewLightPos = mul(float4(g_light[lightIndex].position.xyz, 1.f), g_matView).xyz;
    float distance = length(viewPos - viewLightPos);
    if (distance > g_light[lightIndex].range)
        clip(-1);

    float3 viewNormal = g_tex_1.Sample(g_sam_0, uv).xyz;

    LightColor color = CalculateLightColor(g_int_0, viewNormal, viewPos);

    output.diffuse = color.diffuse + color.ambient;
    output.specular = color.specular;

    return output;
}

// [Final]
// g_tex_0 : Diffuse Color Target
// g_tex_1 : Diffuse Light Target
// g_tex_2 : Specular Light Target
// Mesh : Rectangle

VS_OUT VS_Final(VS_IN input)
{
    VS_OUT output = (VS_OUT)0;

    output.pos = float4(input.pos * 2.f, 1.f);
    output.uv = input.uv;

    return output;
}

float4 PS_Final(VS_OUT input) : SV_Target
{
    float4 output = (float4)0;

    float4 lightPower = g_tex_1.Sample(g_sam_0, input.uv);
    if (lightPower.x == 0.f && lightPower.y == 0.f && lightPower.z == 0.f)
        clip(-1);

    float4 color = g_tex_0.Sample(g_sam_0, input.uv);
    float4 specular = g_tex_2.Sample(g_sam_0, input.uv);

    output = (color * lightPower) + specular;
    return output;
}

#endif