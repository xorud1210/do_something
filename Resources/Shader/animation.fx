#ifndef _ANIMATION_FX_
#define _ANIMATION_FX_

#include "params.fx"
#include "utils.fx"

struct AnimFrameParams
{
    float4 scale;
    float4 rotation;
    float4 translation;
};

StructuredBuffer<AnimFrameParams>   g_bone_frame  : register(t8);
StructuredBuffer<matrix>            g_offset      : register(t9);
StructuredBuffer<int>               g_bone_parent : register(t10);
StructuredBuffer<float>             g_bone_mask   : register(t11);

// 이번 프레임에 섞을 채널들.
// 채널 하나 = 클립 하나를 특정 프레임에서 읽으라는 지시 + 그 가중치.
// 레이어 안의 크로스페이드까지 CPU 가 가중치에 미리 곱해서 넘기므로,
// 셰이더는 "가중치대로 더하기" 하나만 하면 된다.
struct AnimChannel
{
    float4  frame;      // (클립 시작 오프셋, 프레임, 다음 프레임, 보간비율)
    float   weight;
    float3  padding;
};

StructuredBuffer<AnimChannel>       g_anim_channel : register(t12);
RWStructuredBuffer<matrix>          g_final       : register(u0);

// 본 하나의 포즈. 블렌딩은 행렬이 아니라 이 형태에서 해야 한다.
// 행렬을 선형 보간하면 회전이 찌그러진다.
struct BoneSRT
{
    float3 scale;
    float4 rotation;    // quaternion
    float3 translation;
};

// 한 채널(= 재생 중인 클립 하나)에서 본 하나의 포즈를 뽑는다.
//   channel = (클립 시작 오프셋, 현재 프레임, 다음 프레임, 프레임 간 보간비율)
//
// 모든 클립이 한 버퍼에 이어붙어 있어서 클립 오프셋을 더해 접근한다.
// 클립 내부 배치는 [frame][bone] 이므로 boneCount * frame + bone.
BoneSRT SampleChannel(int boneIdx, int boneCount, float4 channel)
{
    int clipOffset = (int) channel.x;
    int curFrame   = (int) channel.y;
    int nextFrame  = (int) channel.z;
    float ratio    = channel.w;

    uint i0 = clipOffset + boneCount * curFrame  + boneIdx;
    uint i1 = clipOffset + boneCount * nextFrame + boneIdx;

    float4 q0 = g_bone_frame[i0].rotation;
    float4 q1 = g_bone_frame[i1].rotation;

    // 최단 경로로 돌도록 부호를 맞춘다.
    if (dot(q0, q1) < 0.f)
        q1 = -q1;

    BoneSRT result;
    result.scale       = lerp(g_bone_frame[i0].scale.xyz,       g_bone_frame[i1].scale.xyz,       ratio);
    result.translation = lerp(g_bone_frame[i0].translation.xyz, g_bone_frame[i1].translation.xyz, ratio);
    result.rotation    = normalize(lerp(q0, q1, ratio));
    return result;
}

// 두 포즈를 섞는다. weight 0 이면 a, 1 이면 b.
BoneSRT BlendSRT(BoneSRT a, BoneSRT b, float weight)
{
    float4 qb = b.rotation;
    if (dot(a.rotation, qb) < 0.f)
        qb = -qb;

    BoneSRT result;
    result.scale       = lerp(a.scale, b.scale, weight);
    result.translation = lerp(a.translation, b.translation, weight);
    result.rotation    = normalize(lerp(a.rotation, qb, weight));
    return result;
}

// 쿼터니언 -> 회전행렬 (행벡터 규약, v' = v * M)
matrix SRTToMatrix(BoneSRT srt)
{
    float x = srt.rotation.x, y = srt.rotation.y, z = srt.rotation.z, w = srt.rotation.w;
    float xx = x * x, yy = y * y, zz = z * z;
    float xy = x * y, xz = x * z, yz = y * z;
    float wx = w * x, wy = w * y, wz = w * z;
    float3 s = srt.scale;

    matrix M;
    M._11 = (1.f - 2.f * (yy + zz)) * s.x;
    M._12 = (2.f * (xy + wz))       * s.x;
    M._13 = (2.f * (xz - wy))       * s.x;
    M._14 = 0.f;

    M._21 = (2.f * (xy - wz))       * s.y;
    M._22 = (1.f - 2.f * (xx + zz)) * s.y;
    M._23 = (2.f * (yz + wx))       * s.y;
    M._24 = 0.f;

    M._31 = (2.f * (xz + wy))       * s.z;
    M._32 = (2.f * (yz - wx))       * s.z;
    M._33 = (1.f - 2.f * (xx + yy)) * s.z;
    M._34 = 0.f;

    M._41 = srt.translation.x;
    M._42 = srt.translation.y;
    M._43 = srt.translation.z;
    M._44 = 1.f;
    return M;
}

// 채널 여러 개를 가중치대로 하나의 포즈로 누적한다.
//
// 2D 블렌드 스페이스는 한 시점에 클립 서너 개가 동시에 필요하다.
// 고정 슬롯 두 개로는 담을 수 없어서 개수를 런타임에 받는다.
BoneSRT AccumulateChannels(int boneIdx, int boneCount, int start, int count)
{
    BoneSRT result;
    result.scale = float3(0.f, 0.f, 0.f);
    result.rotation = float4(0.f, 0.f, 0.f, 0.f);
    result.translation = float3(0.f, 0.f, 0.f);

    float total = 0.f;
    float4 refRotation = float4(0.f, 0.f, 0.f, 1.f);
    bool first = true;

    for (int i = 0; i < count; ++i)
    {
        float weight = g_anim_channel[start + i].weight;
        if (weight <= 0.0001f)
            continue;

        BoneSRT sample = SampleChannel(boneIdx, boneCount, g_anim_channel[start + i].frame);

        // q 와 -q 는 같은 회전이다. 부호가 엇갈린 채로 더하면 서로 상쇄되어
        // 자세가 무너진다. 먼저 들어온 채널을 기준으로 부호를 맞춘다.
        if (first)
        {
            refRotation = sample.rotation;
            first = false;
        }
        else if (dot(refRotation, sample.rotation) < 0.f)
        {
            sample.rotation = -sample.rotation;
        }

        result.scale += sample.scale * weight;
        result.translation += sample.translation * weight;
        result.rotation += sample.rotation * weight;
        total += weight;
    }

    if (total > 0.0001f)
    {
        result.scale /= total;
        result.translation /= total;
        result.rotation = normalize(result.rotation);
    }
    else
    {
        result.scale = float3(1.f, 1.f, 1.f);
        result.rotation = float4(0.f, 0.f, 0.f, 1.f);
        result.translation = float3(0.f, 0.f, 0.f);
    }

    return result;
}

// 본 하나의 로컬 포즈를 만든다.
//
//   기본 레이어 : 전신. 채널 [0, g_int_1) 을 누적한다.
//   상체 레이어 : 마스크가 0 이 아닌 본만. 채널 [g_int_1, g_int_1 + g_int_2).
//
// 마스크는 본별 가중치라 "하체는 달리기, 상체는 공격" 같은 조합이 나온다.
// 섞는 것은 반드시 SRT 공간에서 해야 한다. 계층 조립은 그 다음이다.
matrix GetLayeredBoneMatrix(int boneIdx, int boneCount)
{
    BoneSRT pose = AccumulateChannels(boneIdx, boneCount, 0, g_int_1);

    // g_float_2 는 상체 레이어의 전체 세기. 0 이면 마스크를 읽지도 않는다.
    if (g_float_2 > 0.f && g_int_2 > 0)
    {
        float mask = g_bone_mask[boneIdx] * g_float_2;
        if (mask > 0.f)
        {
            BoneSRT upper = AccumulateChannels(boneIdx, boneCount, g_int_1, g_int_2);
            pose = BlendSRT(pose, upper, mask);
        }
    }

    return SRTToMatrix(pose);
}

// 본 계층의 최대 깊이 안전 상한. 사람형 스켈레톤은 보통 10 안쪽이다.
#define MAX_BONE_DEPTH 64

// ComputeAnimation
// g_int_0   : BoneCount
// g_int_1   : 기본 레이어 채널 개수 (버퍼 앞쪽부터)
// g_int_2   : 상체 레이어 채널 개수 (기본 레이어 뒤에 이어짐)
// g_int_3   : 1 이면 본 프레임이 부모 기준 로컬이라 계층을 다시 조립한다 (.bin)
//             0 이면 이미 모델 공간으로 구워져 있다 (FBX)
// g_float_2 : 상체 레이어 전체 세기
// t8        : 본 프레임 (SRT)
// t9        : 바인드포즈 역행렬
// t10       : 부모 인덱스 (로컬일 때만)
// t11       : 본별 상체 마스크 (레이어가 켜져 있을 때만)
// t12       : 이번 프레임의 채널 목록
// u0        : 본 최종 행렬 (출력)
[numthreads(256, 1, 1)]
void CS_Main(int3 threadIdx : SV_DispatchThreadID)
{
    if (g_int_0 <= threadIdx.x)
        return;

    int boneCount   = g_int_0;
    int localFrames = g_int_3;

    matrix matBone = GetLayeredBoneMatrix(threadIdx.x, boneCount);

    if (localFrames == 1)
    {
        // 로컬 -> 모델 공간. 부모를 타고 루트까지 올라가며 곱한다.
        //   World = ToParent * ParentWorld   (행벡터 규약)
        //
        // 스레드마다 조상 체인을 중복 계산하지만, 본 100여 개 * 깊이 10 수준이라
        // 깊이별 다중 Dispatch 를 쓸 만큼의 비용이 아니다. 대신 코드가 단순하고,
        // 본마다 다른 클립을 섞는 레이어 블렌딩으로 확장하기도 쉽다.
        int parent = g_bone_parent[threadIdx.x];

        [loop]
        for (int depth = 0; depth < MAX_BONE_DEPTH; ++depth)
        {
            if (parent < 0)
                break;

            matBone = mul(matBone, GetLayeredBoneMatrix(parent, boneCount));
            parent = g_bone_parent[parent];
        }
    }

    // g_offset 은 열 우선으로 읽힌다. FBX 경로는 FbxAMatrix 를 그대로 복사해
    // 이미 그 형태고, .bin 은 CPU 쪽(Mesh)에서 전치해 맞춰 놓았다.
    //   최종 = Offset * World   (행벡터 규약)
    g_final[threadIdx.x] = mul(g_offset[threadIdx.x], matBone);
}

#endif
