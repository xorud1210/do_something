#pragma once
#include "Texture.h"

enum class RENDER_TARGET_GROUP_TYPE : uint8
{
	SWAP_CHAIN, // BACK_BUFFER, FRONT_BUFFER
	SHADOW, // SHADOW
	G_BUFFER, // POSITION, NORMAL, COLOR
	LIGHTING, // DIFFUSE LIGHT, SPECULAR LIGHT
	HDR, // 톤매핑 전 씬 한 장. 조명 합성 + 포워드 결과가 여기 모인다
	BLOOM_HALF, // 1/2 해상도. 블러 핑퐁용 2장
	BLOOM_QUARTER, // 1/4 해상도. 블러 핑퐁용 2장
	END,
};

enum
{
	RENDER_TARGET_SHADOW_GROUP_MEMBER_COUNT = 1,
	RENDER_TARGET_G_BUFFER_GROUP_MEMBER_COUNT = 3,
	RENDER_TARGET_LIGHTING_GROUP_MEMBER_COUNT = 2,
	RENDER_TARGET_HDR_GROUP_MEMBER_COUNT = 1,
	RENDER_TARGET_BLOOM_GROUP_MEMBER_COUNT = 2,
	RENDER_TARGET_GROUP_COUNT = static_cast<uint8>(RENDER_TARGET_GROUP_TYPE::END)
};

// 톤매핑 전 단계는 1 을 넘는 밝기를 그대로 들고 가야 한다.
// R8G8B8A8_UNORM 이면 조명이 겹치는 순간 흰색에서 잘려버려서
// 밝은 곳이 얼마나 밝았는지가 사라진다.
constexpr DXGI_FORMAT HDR_RENDER_TARGET_FORMAT = DXGI_FORMAT_R16G16B16A16_FLOAT;

struct RenderTarget
{
	shared_ptr<Texture> target;
	float clearColor[4];
};

class RenderTargetGroup
{
public:
	void Create(RENDER_TARGET_GROUP_TYPE groupType, vector<RenderTarget>& rtVec, shared_ptr<Texture> dsTexture);

	void OMSetRenderTargets(uint32 count, uint32 offset);
	void OMSetRenderTargets();

	void ClearRenderTargetView(uint32 index);
	void ClearRenderTargetView();

	shared_ptr<Texture> GetRTTexture(uint32 index) { return _rtVec[index].target; }
	shared_ptr<Texture> GetDSTexture() { return _dsTexture; }

	void WaitTargetToResource();
	void WaitResourceToTarget();

	// 그룹 안에서 한 장씩 상태를 바꿔야 할 때 쓴다.
	// 블러는 같은 그룹의 0번을 읽어 1번에 그리고, 다시 1번을 읽어 0번에 그리는
	// 핑퐁이라 그룹 전체를 한꺼번에 전이시키면 안 된다.
	void WaitTargetToResource(uint32 index);
	void WaitResourceToTarget(uint32 index);

private:
	RENDER_TARGET_GROUP_TYPE		_groupType;
	vector<RenderTarget>			_rtVec;
	uint32							_rtCount;
	shared_ptr<Texture>				_dsTexture;
	ComPtr<ID3D12DescriptorHeap>	_rtvHeap;

private:
	uint32							_rtvHeapSize;
	D3D12_CPU_DESCRIPTOR_HANDLE		_rtvHeapBegin;
	D3D12_CPU_DESCRIPTOR_HANDLE		_dsvHeapBegin;

private:
	D3D12_RESOURCE_BARRIER			_targetToResource[8];
	D3D12_RESOURCE_BARRIER			_resourceToTarget[8];
};

