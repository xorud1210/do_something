#ifndef _TERRAIN_FX_
#define _TERRAIN_FX_

#include "params.fx"

// 지형.
//
// 졸업작품은 스플랫맵(R/G/B = 흙/풀/바위)을 그려 넣고 층마다 텍스처 두 장을
// 노이즈로 섞었다. 그 스플랫맵이 색 블록 몇 개짜리 테스트 에셋이라 그대로는
// 쓸 수 없어서, 가중치를 경사와 높이에서 만든다.
//
// 결과적으로 하는 일은 같다 - 세 층을 가중치로 섞고, 멀리서는 큰 컬러맵이
// 살아나게 한다. 다른 점은 가중치가 텍스처가 아니라 지형 자체에서 나온다는 것,
// 그래서 지형을 바꿔도 다시 칠할 것이 없다는 것이다.
//
// g_tex_0 : 지형 전체를 덮는 컬러맵 (UV 0~1)
// g_tex_1 : 풀   디테일
// g_tex_2 : 흙   디테일
// g_tex_3 : 바위 디테일
// g_float_0 : 디테일 반복 횟수
// g_vec4_0  : (지형 중심 y, 높이 진폭, -, -)

struct VS_IN
{
    float3 pos : POSITION;
    float2 uv : TEXCOORD;
    float3 normal : NORMAL;
    float3 tangent : TANGENT;
};

struct VS_OUT
{
    float4 pos : SV_Position;
    float2 uv : TEXCOORD;
    float3 viewPos : POSITION;
    float3 viewNormal : NORMAL;
    float3 worldPos : POSITION1;
    float3 worldNormal : NORMAL1;
};

VS_OUT VS_Main(VS_IN input)
{
    VS_OUT output = (VS_OUT) 0.f;

    output.pos = mul(float4(input.pos, 1.f), g_matWVP);
    output.uv = input.uv;
    output.viewPos = mul(float4(input.pos, 1.f), g_matWV).xyz;
    output.viewNormal = normalize(mul(float4(input.normal, 0.f), g_matWV).xyz);
    output.worldPos = mul(float4(input.pos, 1.f), g_matWorld).xyz;
    output.worldNormal = normalize(mul(float4(input.normal, 0.f), g_matWorld).xyz);

    return output;
}

struct PS_OUT
{
    float4 position : SV_Target0;
    float4 normal : SV_Target1;
    float4 color : SV_Target2;
};

// 같은 텍스처를 두 배율로 겹쳐 읽는다.
//
// 한 배율로만 반복하면 격자 무늬가 보인다. 졸업작품은 서로 다른 텍스처 두 장을
// 섞어 그걸 없앴는데, 여기서는 텍스처 슬롯이 네 개뿐이라 같은 장을 다른 배율로
// 겹쳤다. 무늬의 주기가 서로 안 맞아떨어져 반복이 눈에 덜 띈다.
float3 SampleDetail(Texture2D tex, float2 uv, float tiling)
{
    float3 a = tex.Sample(g_sam_0, uv * tiling).rgb;
    float3 b = tex.Sample(g_sam_0, uv * tiling * 0.37f + 0.5f).rgb;
    return lerp(a, b, 0.4f);
}

PS_OUT PS_Main(VS_OUT input)
{
    PS_OUT output = (PS_OUT) 0;

    const float tiling = g_float_0;

    float3 grass = SampleDetail(g_tex_1, input.uv, tiling);
    float3 dirt  = SampleDetail(g_tex_2, input.uv, tiling * 0.8f);
    float3 rock  = SampleDetail(g_tex_3, input.uv, tiling * 1.6f);

    // 경사. 법선의 y 성분이 곧 바닥과의 각도다.
    // 가파른 곳에는 흙이 붙어 있지 못하고 바위가 드러난다.
    float slope = 1.f - saturate(input.worldNormal.y);
    float rockWeight = smoothstep(0.07f, 0.26f, slope);

    // 높이. 낮은 곳은 물이 지나간 자리라 흙, 높은 곳은 풀.
    float centerY = g_vec4_0.x;
    float amplitude = max(g_vec4_0.y, 1.f);
    float relative = (input.worldPos.y - centerY) / amplitude;   // 대략 -0.5 ~ 0.5
    float dirtWeight = smoothstep(0.05f, -0.18f, relative);

    // 경계를 딱 떨어지게 두면 등고선처럼 보인다. 월드 좌표로 만든 값을 섞어
    // 경계를 흔든다. 텍스처를 하나 더 쓰지 않으려고 sin 두 개로 만들었다.
    float jitter = sin(input.worldPos.x * 0.013f) * sin(input.worldPos.z * 0.017f);
    rockWeight = saturate(rockWeight + jitter * 0.12f);
    dirtWeight = saturate(dirtWeight + jitter * 0.10f);

    // 바위가 먼저다. 가파른 곳은 높이와 무관하게 바위다.
    float3 detail = lerp(grass, dirt, dirtWeight);
    detail = lerp(detail, rock, rockWeight);

    // 컬러맵을 곱해 큰 스케일의 색 변화를 얹는다.
    // 곱하면 전체가 어두워지므로 밝기를 되돌려 준다.
    float3 baseColor = g_tex_0.Sample(g_sam_0, input.uv).rgb;
    float3 albedo = detail * baseColor * 1.9f;

    output.position = float4(input.viewPos, 0.f);
    output.normal = float4(input.viewNormal, 0.f);
    output.color = float4(saturate(albedo), 1.f);

    return output;
}

#endif
