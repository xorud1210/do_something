#ifndef _ANIMATION_FX_
#define _ANIMATION_FX_

#include "params.fx"
#include "utils.fx"
#include "math.fx"

struct AnimFrameParams
{
    float4 scale;
    float4 rotation;
    float4 translation;
};

StructuredBuffer<AnimFrameParams>   g_bone_frame  : register(t8);
StructuredBuffer<matrix>            g_offset      : register(t9);
StructuredBuffer<int>               g_bone_parent : register(t10);
RWStructuredBuffer<matrix>          g_final       : register(u0);

// 한 본의 키프레임 SRT 를 보간해 행렬로 만든다.
// 프레임 데이터 배치는 [frame][bone] 이므로 인덱스는 boneCount * frame + bone.
//
// math.fx 의 MatrixAffineTransformation / QuaternionSlerp 을 쓰지 않고 직접 만든다.
// 그 함수들은 Animator 가 씬에 붙은 적이 없어 한 번도 실행되지 않았던 코드다.
matrix GetBoneMatrix(int boneIdx, int boneCount, int curFrame, int nextFrame, float ratio)
{
    uint idx     = (boneCount * curFrame)  + boneIdx;
    uint nextIdx = (boneCount * nextFrame) + boneIdx;

    float3 s0 = g_bone_frame[idx].scale.xyz;
    float3 s1 = g_bone_frame[nextIdx].scale.xyz;
    float3 t0 = g_bone_frame[idx].translation.xyz;
    float3 t1 = g_bone_frame[nextIdx].translation.xyz;
    float4 q0 = g_bone_frame[idx].rotation;
    float4 q1 = g_bone_frame[nextIdx].rotation;

    // 최단 경로로 돌도록 부호를 맞춘 뒤 정규화 보간(nlerp).
    if (dot(q0, q1) < 0.f)
        q1 = -q1;

    float3 s = lerp(s0, s1, ratio);
    float3 t = lerp(t0, t1, ratio);
    float4 q = normalize(lerp(q0, q1, ratio));

    // 쿼터니언 -> 회전행렬 (행벡터 규약, v' = v * M)
    float x = q.x, y = q.y, z = q.z, w = q.w;
    float xx = x * x, yy = y * y, zz = z * z;
    float xy = x * y, xz = x * z, yz = y * z;
    float wx = w * x, wy = w * y, wz = w * z;

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

    M._41 = t.x;
    M._42 = t.y;
    M._43 = t.z;
    M._44 = 1.f;

    return M;
}

// 본 계층의 최대 깊이 안전 상한. 사람형 스켈레톤은 보통 10 안쪽이다.
#define MAX_BONE_DEPTH 64

// ComputeAnimation
// g_int_0   : BoneCount
// g_int_1   : CurrentFrame
// g_int_2   : NextFrame
// g_int_3   : 1 이면 본 프레임이 부모 기준 로컬이라 계층을 다시 조립한다 (.bin)
//             0 이면 이미 모델 공간으로 구워져 있다 (FBX)
// g_float_0 : Ratio
[numthreads(256, 1, 1)]
void CS_Main(int3 threadIdx : SV_DispatchThreadID)
{
    if (g_int_0 <= threadIdx.x)
        return;

    int boneCount    = g_int_0;
    int currentFrame = g_int_1;
    int nextFrame    = g_int_2;
    int localFrames  = g_int_3;
    float ratio      = g_float_0;

    matrix matBone = GetBoneMatrix(threadIdx.x, boneCount, currentFrame, nextFrame, ratio);

    if (localFrames == 1)
    {
        // 로컬 -> 모델 공간. 부모를 타고 루트까지 올라가며 곱한다.
        //   World = ToParent * ParentWorld   (행벡터 규약)
        //
        // 스레드마다 조상 체인을 중복 계산하지만, 본 100여 개 * 깊이 10 수준이라
        // 깊이별 다중 Dispatch 를 쓸 만큼의 비용이 아니다. 대신 코드가 단순해지고
        // 본별로 서로 다른 클립을 섞는 레이어 블렌딩으로 확장하기도 쉽다.
        int parent = g_bone_parent[threadIdx.x];

        [loop]
        for (int depth = 0; depth < MAX_BONE_DEPTH; ++depth)
        {
            if (parent < 0)
                break;

            matBone = mul(matBone, GetBoneMatrix(parent, boneCount, currentFrame, nextFrame, ratio));
            parent = g_bone_parent[parent];
        }
    }

    g_final[threadIdx.x] = mul(g_offset[threadIdx.x], matBone);
}

#endif
