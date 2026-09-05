#ifndef _SHADOW_FX_
#define _SHADOW_FX_

#include "params.fx"
#include "utils.fx"

// 셰도우 맵을 굽는 패스.
//
// 스키닝을 여기서도 해야 한다. 안 하면 화면의 캐릭터는 움직이는데
// 그림자만 바인드 포즈(T 포즈)로 남는다. 정점을 옮기는 주체가
// 디퍼드 셰이더가 아니라 정점 자체이기 때문이다.

struct VS_IN
{
    float3 pos : POSITION;
    float2 uv : TEXCOORD;
    float3 normal : NORMAL;
    float3 tangent : TANGENT;
    float4 weight : WEIGHT;
    float4 indices : INDICES;
};

struct VS_OUT
{
    float4 pos : SV_Position;
    float4 clipPos : POSITION;
};

// g_int_1 : 애니메이션 메시면 1
VS_OUT VS_Main(VS_IN input)
{
    VS_OUT output = (VS_OUT)0.f;

    if (g_int_1 == 1)
        Skinning(input.pos, input.normal, input.tangent, input.weight, input.indices);

    output.pos = mul(float4(input.pos, 1.f), g_matWVP);
    output.clipPos = output.pos;

    return output;
}

float4 PS_Main(VS_OUT input) : SV_Target
{
    return float4(input.clipPos.z / input.clipPos.w, 0.f, 0.f, 0.f);
}

#endif