#ifndef _SKYBOX_FX_
#define _SKYBOX_FX_

#include "params.fx"

struct VS_IN
{
    float3 localPos : POSITION;
    float2 uv : TEXCOORD;
};

struct VS_OUT
{
    float4 pos : SV_Position;
    float2 uv : TEXCOORD;
};

VS_OUT VS_Main(VS_IN input)
{
    VS_OUT output = (VS_OUT)0;

    // Translation은 하지 않고 Rotation만 적용한다
    float4 viewPos = mul(float4(input.localPos, 0), g_matView);
    float4 clipSpacePos = mul(viewPos, g_matProjection);

    // w/w=1이기 때문에 항상 깊이가 1로 유지된다
    output.pos = clipSpacePos.xyww;
    output.uv = input.uv;

    return output;
}

// ACES 근사의 역함수.
//
// 스카이박스 텍스처는 이미 톤매핑이 끝난 사진이다. 그런데 이 패스는 HDR 타겟에
// 그리고, 그 뒤에 포스트프로세스가 ACES 를 한 번 더 먹인다. 곡선을 두 번 타면
// 밝은 쪽이 두 번 눕혀져 하늘이 뿌옇게 뜬다.
//
// 그래서 여기서 먼저 역함수를 걸어 "톤매핑되기 전이었다면 이 값이었을 것"으로
// 되돌려 놓는다. 뒤에서 ACES 가 다시 눕히면 원래 사진으로 돌아온다.
//
// y = (x(ax+b)) / (x(cx+d)+e) 를 x 에 대해 푼 것이다.
// 근이 둘인데 0 근처에서 0 이 되는 쪽을 쓴다.
float3 ACESFilmInverse(float3 y)
{
    return (-0.59f * y + 0.03f
            - sqrt(max(-1.0127f * y * y + 1.3702f * y + 0.0009f, 0.f)))
           / (2.f * (2.43f * y - 2.51f));
}

float4 PS_Main(VS_OUT input) : SV_Target
{
    // 텍스처가 sRGB 포맷이라 여기서 나오는 값은 이미 선형이다.
    float4 color = g_tex_0.Sample(g_sam_0, input.uv);

    return float4(ACESFilmInverse(saturate(color.rgb)), color.a);
}

#endif