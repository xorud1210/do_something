#ifndef _BILLBOARD_FX_
#define _BILLBOARD_FX_

#include "params.fx"

// 인스턴싱 초목 빌보드.
//
// 점 하나를 지오메트리 셰이더에서 사각형으로 펼친다.
// 인스턴스 데이터는 정점 버퍼가 아니라 StructuredBuffer 에서 당겨온다.
// 정점 버퍼에는 점 하나만 있고, 나머지는 인스턴스 ID 로 인덱싱한다.
// 그래서 수천 장을 드로우콜 하나로 그린다.
//
// 파티클과 다른 점은 축 고정(Y-axis locked)이라는 것이다.
// 파티클은 카메라 평면과 완전히 평행한 사각형이라 위에서 내려다보면 눕지만,
// 풀은 땅에 서 있어야 하므로 Y 축은 월드에 고정하고 Y 회전만 카메라를 향한다.

struct BillboardInstance
{
    float3  worldPos;
    float   scale;
    float   phase;      // 인스턴스마다 다른 바람 위상. 없으면 전부 같이 흔들린다
    float3  padding;
};

StructuredBuffer<BillboardInstance> g_billboard : register(t9);

struct VS_IN
{
    float3 pos : POSITION;
    float2 uv : TEXCOORD;
    float3 normal : NORMAL;
    float3 tangent : TANGENT;
    uint id : SV_InstanceID;
};

struct VS_OUT
{
    float4 worldPos : POSITION;
    float  id : ID;
};

VS_OUT VS_Main(VS_IN input)
{
    VS_OUT output = (VS_OUT) 0.f;

    // 점 메시의 정점은 (0,0,0) 이다. 위치는 전부 인스턴스 버퍼에서 온다.
    output.worldPos = float4(g_billboard[input.id].worldPos, 1.f);
    output.id = input.id;

    return output;
}

struct GS_OUT
{
    float4 pos : SV_Position;
    float2 uv : TEXCOORD;
    float3 viewPos : POSITION;
    float3 viewNormal : NORMAL;
};

// GS_Main
// g_float_0 : 누적 시간
// g_float_1 : 바람 세기 (월드 단위)
// g_float_2 : 바람 주파수
// g_float_3 : 가로 대비 세로 비율
// g_vec4_0  : 바람 방향 (xyz)
[maxvertexcount(6)]
void GS_Main(point VS_OUT input[1], inout TriangleStream<GS_OUT> outputStream)
{
    uint id = (uint) input[0].id;
    float3 basePos = input[0].worldPos.xyz;
    float scale = g_billboard[id].scale;
    float phase = g_billboard[id].phase;

    // 카메라의 월드 위치는 뷰 역행렬의 이동 성분이다.
    float3 camWorld = float3(g_matViewInv._41, g_matViewInv._42, g_matViewInv._43);

    // 축 고정 빌보드.
    // 카메라 방향에서 Y 성분을 빼고 XZ 평면에서만 돌린다.
    // 그래야 풀이 항상 땅에 수직으로 서 있는다.
    float3 toCam = camWorld - basePos;
    toCam.y = 0.f;
    toCam = normalize(toCam + float3(0.0001f, 0.f, 0.0001f));

    const float3 up = float3(0.f, 1.f, 0.f);
    float3 right = normalize(cross(up, toCam));

    float halfWidth = scale * 0.5f;
    float height = scale * g_float_3;

    // 바람. 위쪽 두 정점만 밀어서 밑동은 땅에 붙어 있게 한다.
    // 월드 좌표를 위상에 섞으면 파도가 지나가는 것처럼 보인다.
    float wave = sin(g_float_0 * g_float_2 + phase
                   + basePos.x * 0.012f + basePos.z * 0.017f);
    float3 windOffset = normalize(g_vec4_0.xyz + float3(0.0001f, 0.f, 0.f))
                      * wave * g_float_1;

    float3 corner[4];
    corner[0] = basePos - right * halfWidth;                            // 좌하
    corner[1] = basePos + right * halfWidth;                            // 우하
    corner[2] = basePos + right * halfWidth + up * height + windOffset; // 우상
    corner[3] = basePos - right * halfWidth + up * height + windOffset; // 좌상

    float2 uvs[4] =
    {
        float2(0.f, 1.f), float2(1.f, 1.f), float2(1.f, 0.f), float2(0.f, 0.f)
    };

    // 법선.
    // 사각형의 실제 법선을 그대로 쓰면 풀이 카메라를 향한 판때기처럼 번들거린다.
    // 위쪽으로 많이 기울여서 땅과 비슷하게 조명받게 한다.
    float3 worldNormal = normalize(up * 0.7f + toCam * 0.3f);

    GS_OUT output[4];

    [unroll]
    for (int i = 0; i < 4; ++i)
    {
        float4 viewPos = mul(float4(corner[i], 1.f), g_matView);
        output[i].pos = mul(viewPos, g_matProjection);
        output[i].viewPos = viewPos.xyz;
        output[i].viewNormal = normalize(mul(float4(worldNormal, 0.f), g_matView).xyz);
        output[i].uv = uvs[i];
    }

    outputStream.Append(output[0]);
    outputStream.Append(output[3]);
    outputStream.Append(output[2]);
    outputStream.RestartStrip();

    outputStream.Append(output[0]);
    outputStream.Append(output[2]);
    outputStream.Append(output[1]);
    outputStream.RestartStrip();
}

// PS_Main
// g_tex_0 : 초목 텍스처 (알파로 잘라낸다)
//
// 디퍼드 패스라 G-Buffer 세 장에 쓴다. 알파 블렌딩이 아니라 알파 테스트다 -
// 디퍼드는 픽셀 하나에 표면 하나만 담을 수 있어서 반투명을 못 담는다.
struct PS_OUT
{
    float4 position : SV_Target0;
    float4 normal : SV_Target1;
    float4 color : SV_Target2;
};

PS_OUT PS_Main(GS_OUT input)
{
    PS_OUT output = (PS_OUT) 0;

    float4 color = g_tex_0.Sample(g_sam_0, input.uv);

    // 잎 사이의 빈 곳은 아예 버린다.
    clip(color.a - 0.35f);

    output.position = float4(input.viewPos, 0.f);
    output.normal = float4(input.viewNormal, 0.f);
    output.color = float4(color.rgb, 1.f);

    return output;
}

#endif
