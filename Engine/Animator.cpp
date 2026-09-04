#include "pch.h"
#include "Animator.h"
#include "Timer.h"
#include "Resources.h"
#include "Material.h"
#include "Mesh.h"
#include "MeshRenderer.h"
#include "StructuredBuffer.h"

Animator::Animator() : Component(COMPONENT_TYPE::ANIMATOR)
{
	_computeMaterial = GET_SINGLE(Resources)->Get<Material>(L"ComputeAnimation");
	_boneFinalMatrix = make_shared<StructuredBuffer>();
}

Animator::~Animator()
{
}

void Animator::FinalUpdate()
{
	if (_animClips == nullptr || _animClips->empty())
		return;

	_updateTime += DELTA_TIME;

	const AnimClipInfo& animClip = _animClips->at(_clipIndex);
	if (animClip.duration <= 0.0 || animClip.frameCount <= 0)
		return;

	if (_updateTime >= animClip.duration)
		_updateTime = 0.f;

	// 초당 프레임 수. frameCount / duration 이므로 클립마다 다르다.
	const float framesPerSecond =
		static_cast<float>(animClip.frameCount) / static_cast<float>(animClip.duration);

	const float exactFrame = _updateTime * framesPerSecond;

	_frame = static_cast<int32>(exactFrame);
	_frame = min(_frame, animClip.frameCount - 1);
	_nextFrame = min(_frame + 1, animClip.frameCount - 1);

	// 두 키프레임 사이의 보간 비율.
	// (원래는 _frame - _frame 이라 항상 0 이었고, 그래서 보간이 전혀 안 됐다)
	_frameRatio = exactFrame - static_cast<float>(_frame);
	_frameRatio = std::clamp(_frameRatio, 0.f, 1.f);

}

void Animator::SetAnimClip(const vector<AnimClipInfo>* animClips)
{
	_animClips = animClips;
}

void Animator::PushData()
{
	if (_bones == nullptr || _animClips == nullptr || _animClips->empty())
		return;

	shared_ptr<Mesh> mesh = GetGameObject()->GetMeshRenderer()->GetMesh();
	if (mesh == nullptr || mesh->IsAnimMesh() == false)
		return;

	uint32 boneCount = static_cast<uint32>(_bones->size());
	if (_boneFinalMatrix->GetElementCount() < boneCount)
		_boneFinalMatrix->Init(sizeof(Matrix), boneCount);

	// Compute Shader
	mesh->GetBoneFrameDataBuffer(_clipIndex)->PushComputeSRVData(SRV_REGISTER::t8);
	mesh->GetBoneOffsetBuffer()->PushComputeSRVData(SRV_REGISTER::t9);

	// 본 프레임이 부모 기준 로컬이면(.bin) 계층을 다시 조립해야 하므로
	// 부모 인덱스 테이블을 함께 넘긴다. FBX 는 이미 글로벌이라 필요 없다.
	const bool composeHierarchy =
		mesh->AreBoneFramesLocal() && mesh->GetBoneParentBuffer() != nullptr;

	if (composeHierarchy)
		mesh->GetBoneParentBuffer()->PushComputeSRVData(SRV_REGISTER::t10);

	_boneFinalMatrix->PushComputeUAVData(UAV_REGISTER::u0);

	_computeMaterial->SetInt(0, boneCount);
	_computeMaterial->SetInt(1, _frame);
	_computeMaterial->SetInt(2, _nextFrame);
	_computeMaterial->SetInt(3, composeHierarchy ? 1 : 0);
	_computeMaterial->SetFloat(0, _frameRatio);

	uint32 groupCount = (boneCount / 256) + 1;
	_computeMaterial->Dispatch(groupCount, 1, 1);

	// Graphics Shader
	_boneFinalMatrix->PushGraphicsData(SRV_REGISTER::t7);
}

void Animator::Play(uint32 idx)
{
	assert(_animClips != nullptr && idx < _animClips->size());
	_clipIndex = idx;
	_updateTime = 0.f;
}
