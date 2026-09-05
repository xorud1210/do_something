#ifndef _FOLIAGE_CULL_FX_
#define _FOLIAGE_CULL_FX_

#include "params.fx"

// 초목 절두체 컬링.
//
// 빌보드 4000장을 매 프레임 전부 그리면 카메라 뒤에 있는 것까지 정점 셰이더와
// 지오메트리 셰이더를 통과한다. 사각형으로 펼쳐진 뒤 래스터라이저가 버리므로
// 화면에는 안 보이지만 비용은 이미 다 냈다.
//
// CPU 에서 걸러도 되지만, 그러면 매 프레임 4000번의 판정 결과를 다시 GPU 로
// 올려야 한다. 데이터는 이미 GPU 에 있으니 판정도 GPU 에서 하고 결과도
// GPU 에 남긴다. CPU 는 "몇 장인지" 조차 알 필요가 없다 - 그리기 인자를
// 버퍼에 써 두고 ExecuteIndirect 로 넘긴다.

struct BillboardInstance
{
    float3  worldPos;
    float   scale;
    float   phase;
    float3  padding;
};

// 원본(심을 때 한 번 만들고 안 바뀐다) -> 통과한 것만 추린 목록
StructuredBuffer<BillboardInstance>   g_source  : register(t9);
RWStructuredBuffer<BillboardInstance> g_visible : register(u0);

// D3D12_DRAW_INDEXED_ARGUMENTS 와 배치가 같다.
//   [0] IndexCountPerInstance
//   [1] InstanceCount        <- 이 값을 여기서 만든다
//   [2] StartIndexLocation
//   [3] BaseVertexLocation
//   [4] StartInstanceLocation
RWStructuredBuffer<uint> g_args : register(u1);

// 절두체 평면 6장을 뷰-투영 행렬에서 직접 뽑는다.
//
// 클립 공간에서 점이 절두체 안에 있다는 것은 -w <= x <= w, -w <= y <= w,
// 0 <= z <= w 라는 뜻이다. clip.x 는 월드 좌표와 행렬 '열' 0 의 내적이므로,
// 이 부등식들을 그대로 월드 공간 평면식 dot(n, p) + d >= 0 으로 읽을 수 있다.
//
// 8개 꼭짓점을 역투영해서 평면을 만드는 방법보다 짧고, 무엇보다
// 행렬 하나만 넘기면 되므로 상수 버퍼에 평면 6장을 따로 실어 보낼 필요가 없다.
void BuildFrustumPlanes(out float4 planes[6])
{
    float4x4 m = g_mat_0;

    float4 c0 = float4(m._11, m._21, m._31, m._41);     // clip.x 의 계수
    float4 c1 = float4(m._12, m._22, m._32, m._42);     // clip.y
    float4 c2 = float4(m._13, m._23, m._33, m._43);     // clip.z
    float4 c3 = float4(m._14, m._24, m._34, m._44);     // clip.w

    planes[0] = c3 + c0;    // 좌   ( x >= -w )
    planes[1] = c3 - c0;    // 우   ( x <=  w )
    planes[2] = c3 + c1;    // 하
    planes[3] = c3 - c1;    // 상
    planes[4] = c2;         // 근   ( z >= 0. D3D 는 OpenGL 과 달리 z 범위가 [0,w] 다 )
    planes[5] = c3 - c2;    // 원

    // 법선을 정규화해야 dot + d 가 '실제 거리' 가 된다.
    // 안 하면 반지름과 비교할 수 없다.
    [unroll]
    for (int i = 0; i < 6; ++i)
        planes[i] /= max(length(planes[i].xyz), 0.00001f);
}

// CS_Main
// g_int_0   : 원본 인스턴스 개수
// g_float_0 : 가로 대비 세로 비율 (바운딩 구의 높이를 잡는 데 쓴다)
// g_float_1 : 바람이 밀어낼 수 있는 최대 거리 (반지름에 더해 둔다)
// g_float_2 : 최대 표시 거리. 0 이면 거리 제한 없음
// g_vec4_0  : 카메라 월드 위치
// g_mat_0   : 뷰-투영 행렬
[numthreads(256, 1, 1)]
void CS_Main(int3 threadID : SV_DispatchThreadID)
{
    uint index = (uint) threadID.x;
    if (index >= (uint) g_int_0)
        return;

    BillboardInstance inst = g_source[index];

    // 판정 대상은 점이 아니라 '펼쳐진 뒤의 사각형' 이다.
    // worldPos 는 밑동이므로 그것만 보면 위쪽이 화면에 걸쳐 있는 풀이 통째로 사라진다.
    float width = inst.scale;
    float height = inst.scale * g_float_0;

    float3 center = inst.worldPos + float3(0.f, height * 0.5f, 0.f);
    float radius = 0.5f * sqrt(width * width + height * height) + g_float_1;

    float4 planes[6];
    BuildFrustumPlanes(planes);

    bool visible = true;

    [unroll]
    for (int i = 0; i < 6; ++i)
    {
        // 구의 중심이 평면 뒤로 반지름보다 더 들어갔으면 완전히 밖이다.
        if (dot(planes[i].xyz, center) + planes[i].w < -radius)
            visible = false;
    }

    // 거리 컷오프. 절두체 안이어도 너무 멀면 화면에서 한 픽셀도 안 되므로 버린다.
    if (g_float_2 > 0.f && distance(center, g_vec4_0.xyz) > g_float_2)
        visible = false;

    if (visible == false)
        return;

    // 통과한 것들이 앞에서부터 빈틈없이 채워지도록 자리를 하나 받아 간다.
    // 반환값이 곧 이 스레드가 쓸 인덱스다. 순서는 뒤섞이지만 풀에는 상관없다.
    uint slot;
    InterlockedAdd(g_args[1], 1, slot);

    g_visible[slot] = inst;
}

#endif
