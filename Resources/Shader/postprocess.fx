#ifndef _POSTPROCESS_FX_
#define _POSTPROCESS_FX_

#include "params.fx"

// 화면 전체를 덮는 사각형 하나로 도는 패스들.
// 조명까지 끝난 HDR 씬을 받아서 밝은 부분을 뽑고, 흐리고, 다시 합쳐
// 마지막에 0~1 로 눌러 담아 스왑체인으로 내보낸다.
//
//   HDR 씬 -> BrightPass -> Blur(가로) -> Blur(세로) -> Tonemap -> 백버퍼
//
// 여기서 쓰는 샘플러는 g_sam_1 (선형 + Clamp) 이다.
// 기본 샘플러 g_sam_0 은 Wrap 이라 화면 가장자리에서 반대편 픽셀을 끌어와
// 블러가 테두리를 물들인다.

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

// Rectangle 메시가 ±0.5 라서 2배로 늘리면 NDC 전체를 덮는다.
VS_OUT VS_Main(VS_IN input)
{
    VS_OUT output = (VS_OUT) 0;

    output.pos = float4(input.pos * 2.f, 1.f);
    output.uv = input.uv;

    return output;
}

float Luminance(float3 color)
{
    return dot(color, float3(0.2126f, 0.7152f, 0.0722f));
}

// [BrightPass]
// g_tex_0   : HDR 씬
// g_float_0 : 임계값. 이 밝기를 넘는 만큼만 블룸이 된다
// g_float_1 : 소프트 니 폭
//
// 임계값에서 뚝 끊으면 밝기가 조금만 흔들려도 블룸이 켜졌다 꺼졌다 한다.
// 임계값 주변 구간을 2차 곡선으로 부드럽게 올려서 그 깜빡임을 없앤다.
float4 PS_BrightPass(VS_OUT input) : SV_Target
{
    float3 color = g_tex_0.Sample(g_sam_1, input.uv).rgb;

    float threshold = g_float_0;
    float knee = max(g_float_1, 0.0001f);

    float lum = Luminance(color);

    float soft = clamp(lum - threshold + knee, 0.f, 2.f * knee);
    soft = (soft * soft) / (4.f * knee);

    // 니 구간 안에서는 곡선을, 그 위로는 그냥 초과분을 쓴다.
    float contribution = max(soft, lum - threshold) / max(lum, 0.0001f);

    return float4(color * contribution, 1.f);
}

// [Blur] 분리형 가우시안
// g_tex_0  : 입력
// g_vec2_0 : 한 스텝의 UV 이동량. 가로 패스면 (1/w, 0), 세로 패스면 (0, 1/h)
//
// 2차원 가우시안은 가로 한 번 * 세로 한 번으로 쪼갤 수 있다.
// N x N 탭이 N + N 탭으로 줄어든다.
//
// 여기에 더해 9탭을 5탭으로 접었다. 이웃한 두 탭의 중간 지점을 선형 필터링으로
// 한 번에 읽으면 두 탭을 가중치대로 섞은 값이 그대로 나온다.
// offsets 가 1, 2 같은 정수가 아니라 1.3846... 인 게 그 중간 지점이다.
float4 PS_Blur(VS_OUT input) : SV_Target
{
    const float offsets[3] = { 0.f, 1.3846153846f, 3.2307692308f };
    const float weights[3] = { 0.2270270270f, 0.3162162162f, 0.0702702703f };

    float2 stepUV = g_vec2_0;

    float3 result = g_tex_0.Sample(g_sam_1, input.uv).rgb * weights[0];

    [unroll]
    for (int i = 1; i < 3; ++i)
    {
        float2 delta = stepUV * offsets[i];
        result += g_tex_0.Sample(g_sam_1, input.uv + delta).rgb * weights[i];
        result += g_tex_0.Sample(g_sam_1, input.uv - delta).rgb * weights[i];
    }

    return float4(result, 1.f);
}

// Narkowicz 의 ACES 근사 곡선.
// 1 에서 싹둑 자르는 대신 밝은 쪽을 눕혀서, 과하게 밝은 곳도 흰 덩어리가 아니라
// 밝기 차이가 남아 있게 만든다.
float3 ACESFilm(float3 x)
{
    const float a = 2.51f;
    const float b = 0.03f;
    const float c = 2.43f;
    const float d = 0.59f;
    const float e = 0.14f;

    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

// [Tonemap]
// g_tex_0   : HDR 씬
// g_tex_1   : 블룸 1/2 해상도
// g_tex_2   : 블룸 1/4 해상도
// g_float_0 : 노출
// g_float_1 : 블룸 세기
//
// 해상도가 다른 블룸 두 장을 더하는 이유는 번지는 폭을 넓히기 위해서다.
// 1/4 쪽이 같은 탭 수로 두 배 넓게 퍼진다.
float4 PS_Tonemap(VS_OUT input) : SV_Target
{
    float3 color = g_tex_0.Sample(g_sam_1, input.uv).rgb;

    float3 bloom = g_tex_1.Sample(g_sam_1, input.uv).rgb
                 + g_tex_2.Sample(g_sam_1, input.uv).rgb;

    color += bloom * g_float_1;
    color *= g_float_0;

    return float4(ACESFilm(color), 1.f);
}

#endif
