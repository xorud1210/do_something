#ifndef _PARTICLE_FX_
#define _PARTICLE_FX_

#include "params.fx"
#include "utils.fx"

// GPU 파티클.
//   CS_Main  : 수명/이동을 컴퓨트에서 갱신한다. CPU 는 몇 개 살릴지만 알려준다.
//   VS -> GS : 점 하나를 뷰 공간에서 사각형으로 펼친다. 항상 카메라를 향한다.
//   PS       : 수명에 따라 색과 알파를 보간하고, 지면과 만나는 경계를 페이드한다.
//
// 사각형을 뷰 공간에서 만드는 것이 곧 빌보드다.
// 뷰 공간에서 XY 로만 벌리면 카메라 평면과 평행한 사각형이 되기 때문이다.

struct Particle
{
    float3  worldPos;
    float   curTime;
    float3  worldDir;
    float   lifeTime;
    int     alive;
    float   rotation;       // 현재 회전각 (라디안)
    float   rotationSpeed;
    float   seed;           // 파티클마다 고정된 난수. 색을 조금씩 흔드는 데 쓴다
};

StructuredBuffer<Particle> g_data : register(t9);

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
    float4 viewPos : POSITION;
    float2 uv : TEXCOORD;
    float id : ID;
};

VS_OUT VS_Main(VS_IN input)
{
    VS_OUT output = (VS_OUT) 0.f;

    float3 worldPos = mul(float4(input.pos, 1.f), g_matWorld).xyz;
    worldPos += g_data[input.id].worldPos;

    output.viewPos = mul(float4(worldPos, 1.f), g_matView);
    output.uv = input.uv;
    output.id = input.id;

    return output;
}

struct GS_OUT
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD;
    float2 life : LIFE;      // x = 수명 비율, y = seed
    float viewZ : VIEWZ;     // 소프트 파티클용. 이 조각의 뷰 공간 깊이
};

// GS_Main
// g_float_0 : 시작 크기
// g_float_1 : 끝 크기
[maxvertexcount(6)]
void GS_Main(point VS_OUT input[1], inout TriangleStream<GS_OUT> outputStream)
{
    GS_OUT output[4] =
    {
        (GS_OUT) 0.f, (GS_OUT) 0.f, (GS_OUT) 0.f, (GS_OUT) 0.f
    };

    VS_OUT vtx = input[0];
    uint id = (uint) vtx.id;
    if (0 == g_data[id].alive)
        return;

    float ratio = saturate(g_data[id].curTime / max(g_data[id].lifeTime, 0.0001f));
    float scale = lerp(g_float_0, g_float_1, ratio) * 0.5f;

    // 회전. 뷰 공간에서 XY 를 돌리므로 화면 안에서 도는 것처럼 보인다.
    float s, c;
    sincos(g_data[id].rotation, s, c);

    float2 corners[4] =
    {
        float2(-scale,  scale),
        float2( scale,  scale),
        float2( scale, -scale),
        float2(-scale, -scale)
    };

    [unroll]
    for (int i = 0; i < 4; ++i)
    {
        float2 r = float2(corners[i].x * c - corners[i].y * s,
                          corners[i].x * s + corners[i].y * c);

        float4 viewPos = vtx.viewPos + float4(r, 0.f, 0.f);

        output[i].position = mul(viewPos, g_matProjection);
        output[i].life = float2(ratio, g_data[id].seed);
        output[i].viewZ = viewPos.z;
    }

    output[0].uv = float2(0.f, 0.f);
    output[1].uv = float2(1.f, 0.f);
    output[2].uv = float2(1.f, 1.f);
    output[3].uv = float2(0.f, 1.f);

    outputStream.Append(output[0]);
    outputStream.Append(output[1]);
    outputStream.Append(output[2]);
    outputStream.RestartStrip();

    outputStream.Append(output[0]);
    outputStream.Append(output[2]);
    outputStream.Append(output[3]);
    outputStream.RestartStrip();
}

// PS 공통
// g_tex_0   : 파티클 텍스처
// g_tex_1   : G-Buffer position (뷰 공간). 소프트 파티클용
// g_float_2 : 발광 세기. 1 보다 크면 HDR 범위로 올라가 블룸이 문다
// g_float_3 : 소프트 페이드 거리. 0 이면 끈다
// g_vec2_0  : 렌더타겟 해상도
// g_vec4_0  : 시작 색 (rgba)
// g_vec4_1  : 끝 색 (rgba)
float4 SampleParticle(GS_OUT input)
{
    float4 tex = g_tex_0.Sample(g_sam_0, input.uv);
    float4 grad = lerp(g_vec4_0, g_vec4_1, input.life.x);

    float4 color = tex * grad;

    // 파티클마다 밝기를 조금씩 흔들어 준다. 전부 똑같으면 판박이로 보인다.
    color.rgb *= lerp(0.75f, 1.25f, input.life.y);

    // 소프트 파티클.
    // 사각형이 지면을 뚫고 들어가면 교차선이 칼같이 생긴다.
    // 뒤에 있는 실제 물체와의 깊이 차가 작을수록 투명하게 만들어 그 선을 없앤다.
    if (g_float_3 > 0.f && g_tex_on_1 == 1)
    {
        float2 screenUV = input.position.xy / g_vec2_0;
        float sceneZ = g_tex_1.SampleLevel(g_sam_1, screenUV, 0).z;

        // position 타겟은 하늘처럼 아무것도 없는 곳에서 0 이다. 그때는 페이드하지 않는다.
        if (sceneZ > 0.f)
            color.a *= saturate((sceneZ - input.viewZ) / g_float_3);
    }

    return color;
}

// 알파 블렌딩용. rgb 와 a 를 분리해서 낸다.
float4 PS_Main(GS_OUT input) : SV_Target
{
    float4 color = SampleParticle(input);
    color.rgb *= g_float_2;
    return color;
}

// 가산 블렌딩용. dest += src.rgb 라서 알파가 rgb 에 미리 곱해져 있어야 한다.
float4 PS_Additive(GS_OUT input) : SV_Target
{
    float4 color = SampleParticle(input);
    return float4(color.rgb * color.a * g_float_2, 1.f);
}

struct ComputeShared
{
    int addCount;
    float3 padding;
};

RWStructuredBuffer<Particle> g_particle : register(u0);
RWStructuredBuffer<ComputeShared> g_shared : register(u1);

// 이미터 모양
#define EMITTER_POINT   0
#define EMITTER_SPHERE  1
#define EMITTER_BOX     2
#define EMITTER_CONE    3

// CS_Main
// g_int_0  : 최대 개수 (스레드 그룹 하나에 맞춰 1024 이하)
// g_int_1  : 이번 프레임에 살릴 개수
// g_int_2  : 이미터 모양
// g_vec2_1 : (deltaTime, accTime)
// g_vec4_0 : (최소 수명, 최대 수명, 최소 속도, 최대 속도)
// g_vec4_1 : (이미터 반지름, 콘 각도 cos, 감속 계수, 최대 회전 속도)
// g_vec4_2 : 중력 (xyz)
[numthreads(1024, 1, 1)]
void CS_Main(int3 threadIndex : SV_DispatchThreadID)
{
    if (threadIndex.x >= g_int_0)
        return;

    int maxCount = g_int_0;
    int addCount = g_int_1;
    int shape = g_int_2;

    float deltaTime = g_vec2_1.x;
    float accTime = g_vec2_1.y;

    float minLifeTime = g_vec4_0.x;
    float maxLifeTime = g_vec4_0.y;
    float minSpeed = g_vec4_0.z;
    float maxSpeed = g_vec4_0.w;

    float radius = g_vec4_1.x;
    float coneCos = g_vec4_1.y;
    float drag = g_vec4_1.z;
    float maxSpin = g_vec4_1.w;

    float3 gravity = g_vec4_2.xyz;

    g_shared[0].addCount = addCount;
    GroupMemoryBarrierWithGroupSync();

    if (g_particle[threadIndex.x].alive == 0)
    {
        // 죽어 있는 슬롯끼리 "이번에 살아날 자리"를 먼저 잡는 경쟁을 한다.
        while (true)
        {
            int remaining = g_shared[0].addCount;
            if (remaining <= 0)
                break;

            int expected = remaining;
            int desired = remaining - 1;
            int originalValue;
            InterlockedCompareExchange(g_shared[0].addCount, expected, desired, originalValue);

            if (originalValue == expected)
            {
                g_particle[threadIndex.x].alive = 1;
                break;
            }
        }

        if (g_particle[threadIndex.x].alive == 1)
        {
            float x = ((float) threadIndex.x / (float) maxCount) + accTime;

            float r1 = Rand(float2(x, accTime));
            float r2 = Rand(float2(x * accTime, accTime));
            float r3 = Rand(float2(x * accTime * accTime, accTime * accTime));
            float r4 = Rand(float2(r1 + r2, r3));

            // Rand 는 [0.5, 1] 을 준다. [-1, 1] 로 편다.
            float3 noise = float3(2 * r1 - 1, 2 * r2 - 1, 2 * r3 - 1);
            float3 unit = (noise - 0.5f) * 2.f;
            float3 dir = normalize(unit + 0.0001f);

            float3 spawnPos = (float3) 0.f;

            if (shape == EMITTER_SPHERE)
            {
                // 반지름 안쪽에 고르게. 세제곱근을 취해야 부피에 균일해진다.
                spawnPos = dir * radius * pow(saturate(r4), 1.f / 3.f);
            }
            else if (shape == EMITTER_BOX)
            {
                spawnPos = unit * radius;
            }
            else if (shape == EMITTER_CONE)
            {
                // 바닥의 원에서 태우고, 방향은 위쪽 콘 안으로 제한한다.
                spawnPos = float3(unit.x, 0.f, unit.z) * radius * sqrt(saturate(r4));

                float cosTheta = lerp(1.f, coneCos, saturate(r1));
                float sinTheta = sqrt(saturate(1.f - cosTheta * cosTheta));
                float phi = r2 * 6.2831853f;
                dir = float3(cos(phi) * sinTheta, cosTheta, sin(phi) * sinTheta);
            }

            g_particle[threadIndex.x].worldPos = spawnPos;
            g_particle[threadIndex.x].worldDir = dir * lerp(minSpeed, maxSpeed, saturate(r3));
            g_particle[threadIndex.x].lifeTime = lerp(minLifeTime, maxLifeTime, saturate(r1));
            g_particle[threadIndex.x].curTime = 0.f;
            g_particle[threadIndex.x].rotation = r2 * 6.2831853f;
            g_particle[threadIndex.x].rotationSpeed = (2 * r4 - 1) * maxSpin;
            g_particle[threadIndex.x].seed = saturate(r4);
        }
    }
    else
    {
        g_particle[threadIndex.x].curTime += deltaTime;
        if (g_particle[threadIndex.x].lifeTime < g_particle[threadIndex.x].curTime)
        {
            g_particle[threadIndex.x].alive = 0;
            return;
        }

        // worldDir 을 방향이 아니라 속도로 쓴다. 중력과 감속이 여기 누적된다.
        float3 velocity = g_particle[threadIndex.x].worldDir;
        velocity += gravity * deltaTime;
        velocity *= saturate(1.f - drag * deltaTime);

        g_particle[threadIndex.x].worldDir = velocity;
        g_particle[threadIndex.x].worldPos += velocity * deltaTime;
        g_particle[threadIndex.x].rotation += g_particle[threadIndex.x].rotationSpeed * deltaTime;
    }
}

#endif
