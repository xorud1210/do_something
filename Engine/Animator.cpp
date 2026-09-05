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

void Animator::SetAnimClip(const vector<AnimClipInfo>* animClips)
{
	_animClips = animClips;
}

int32 Animator::FindClip(const wstring& name) const
{
	if (_animClips == nullptr)
		return -1;

	for (int32 i = 0; i < static_cast<int32>(_animClips->size()); i++)
	{
		if (_animClips->at(i).animName == name)
			return i;
	}
	return -1;
}

// ---------------------------------------------------------------------------
// 재생 제어
// ---------------------------------------------------------------------------

bool Animator::StartClip(AnimLayer& layer, int32 clipIndex, float fadeDuration, bool loop)
{
	if (_animClips == nullptr || clipIndex < 0 || clipIndex >= static_cast<int32>(_animClips->size()))
		return false;

	if (fadeDuration > 0.f)
	{
		layer.previous = layer.current;
		layer.fadeElapsed = 0.f;
		layer.fadeDuration = fadeDuration;
	}
	else
	{
		layer.fadeDuration = 0.f;
	}

	layer.current.clipIndex = clipIndex;
	layer.current.updateTime = 0.f;
	layer.current.frame = 0;
	layer.current.nextFrame = 0;
	layer.current.frameRatio = 0.f;
	layer.current.loop = loop;
	layer.finished = false;
	return true;
}

void Animator::Play(uint32 idx, float fadeDuration, bool loop)
{
	if (_animClips == nullptr || idx >= _animClips->size())
		return;

	// 같은 클립을 다시 요청하면 무시한다. 그렇지 않으면 입력이 들어오는 매 프레임
	// 재생 위치가 0 으로 되감겨 애니메이션이 멈춘 것처럼 보인다.
	if (static_cast<int32>(idx) == _base.current.clipIndex && _base.IsFading() == false)
		return;

	StartClip(_base, static_cast<int32>(idx), fadeDuration, loop);
}

bool Animator::PlayByName(const wstring& name, float fadeDuration, bool loop)
{
	const int32 idx = FindClip(name);
	if (idx < 0)
		return false;

	Play(static_cast<uint32>(idx), fadeDuration, loop);
	return true;
}

bool Animator::PlayUpperLayer(const wstring& clipName, float fadeDuration, bool loop)
{
	if (_boneMask == nullptr)
		return false;

	const int32 idx = FindClip(clipName);
	if (idx < 0)
		return false;

	if (StartClip(_upper, idx, fadeDuration, loop) == false)
		return false;

	// 레이어 자체를 켠다. fadeDuration 동안 0 -> 1.
	_layerTarget = 1.f;
	_layerFadeSpeed = (fadeDuration > 0.f) ? (1.f / fadeDuration) : 1000.f;
	return true;
}

void Animator::StopUpperLayer(float fadeDuration)
{
	_layerTarget = 0.f;
	_layerFadeSpeed = (fadeDuration > 0.f) ? (1.f / fadeDuration) : 1000.f;
}

// ---------------------------------------------------------------------------
// 상체 마스크
// ---------------------------------------------------------------------------

bool Animator::BuildUpperBodyMask(const wstring& rootBoneName, int32 featherBones)
{
	if (_bones == nullptr || _bones->empty())
		return false;

	const int32 boneCount = static_cast<int32>(_bones->size());

	int32 rootIndex = -1;
	for (int32 i = 0; i < boneCount; i++)
	{
		if (_bones->at(i).boneName == rootBoneName)
		{
			rootIndex = i;
			break;
		}
	}
	if (rootIndex < 0)
		return false;

	vector<float> mask(boneCount, 0.f);

	for (int32 i = 0; i < boneCount; i++)
	{
		// 이 본이 rootIndex 의 자손인지, 몇 단계 아래인지 센다.
		int32 depth = 0;
		int32 cursor = i;
		bool isUpper = false;

		while (cursor >= 0)
		{
			if (cursor == rootIndex)
			{
				isUpper = true;
				break;
			}
			cursor = _bones->at(cursor).parentIdx;
			depth++;

			if (depth > boneCount)		// 순환 방어
				break;
		}

		if (isUpper == false)
			continue;

		// 경계에서 뚝 끊기면 허리가 접힌 것처럼 보인다.
		// root 부터 featherBones 개까지 0 -> 1 로 서서히 올린다.
		if (featherBones <= 0)
			mask[i] = 1.f;
		else
			mask[i] = std::clamp(static_cast<float>(depth + 1) / static_cast<float>(featherBones + 1), 0.f, 1.f);
	}

	_boneMask = make_shared<StructuredBuffer>();
	_boneMask->Init(sizeof(float), static_cast<uint32>(mask.size()), mask.data());
	return true;
}

// ---------------------------------------------------------------------------
// 시간 진행
// ---------------------------------------------------------------------------

bool Animator::AdvanceChannel(AnimChannel& channel, float deltaTime)
{
	const AnimClipInfo& clip = _animClips->at(channel.clipIndex);
	if (clip.duration <= 0.0 || clip.frameCount <= 0)
		return false;

	channel.updateTime += deltaTime;

	bool finished = false;
	if (channel.updateTime >= clip.duration)
	{
		if (channel.loop)
			channel.updateTime = std::fmod(channel.updateTime, static_cast<float>(clip.duration));
		else
		{
			channel.updateTime = static_cast<float>(clip.duration);
			finished = true;
		}
	}

	// 초당 프레임 수는 클립마다 다르다 (frameCount / duration).
	const float framesPerSecond =
		static_cast<float>(clip.frameCount) / static_cast<float>(clip.duration);
	const float exactFrame = channel.updateTime * framesPerSecond;

	channel.frame = min(static_cast<int32>(exactFrame), clip.frameCount - 1);

	// 루프 클립은 마지막 프레임에서 0 번으로 이어져야 이음매가 생기지 않는다.
	if (channel.frame >= clip.frameCount - 1)
		channel.nextFrame = channel.loop ? 0 : clip.frameCount - 1;
	else
		channel.nextFrame = channel.frame + 1;

	// 두 키프레임 사이의 보간 비율.
	channel.frameRatio = std::clamp(exactFrame - static_cast<float>(channel.frame), 0.f, 1.f);

	return finished;
}

void Animator::UpdateLayer(AnimLayer& layer, float deltaTime)
{
	layer.finished = AdvanceChannel(layer.current, deltaTime);

	if (layer.IsFading())
	{
		// 빠져나가는 클립도 계속 돌려야 섞이는 동안 어색하지 않다.
		AdvanceChannel(layer.previous, deltaTime);

		layer.fadeElapsed += deltaTime;
		if (layer.fadeElapsed >= layer.fadeDuration)
			layer.fadeDuration = 0.f;
	}
}

void Animator::FinalUpdate()
{
	if (_animClips == nullptr || _animClips->empty())
		return;

	const float deltaTime = DELTA_TIME;

	UpdateLayer(_base, deltaTime);

	// 상체 레이어는 세기가 0 이 아닐 때만 돌린다.
	if (_layerWeight > 0.f || _layerTarget > 0.f)
	{
		UpdateLayer(_upper, deltaTime);

		const float step = _layerFadeSpeed * deltaTime;
		if (_layerWeight < _layerTarget)
			_layerWeight = min(_layerWeight + step, _layerTarget);
		else
			_layerWeight = max(_layerWeight - step, _layerTarget);
	}
}

// ---------------------------------------------------------------------------
// GPU 전달
// ---------------------------------------------------------------------------

Vec4 Animator::PackChannel(const AnimChannel& channel)
{
	shared_ptr<Mesh> mesh = GetGameObject()->GetMeshRenderer()->GetMesh();
	const float clipOffset = static_cast<float>(mesh->GetClipFrameOffset(channel.clipIndex));

	return Vec4(clipOffset,
		static_cast<float>(channel.frame),
		static_cast<float>(channel.nextFrame),
		channel.frameRatio);
}

bool Animator::PushBoneData()
{
	if (_boneFinalMatrix == nullptr || _boneFinalMatrix->GetElementCount() == 0)
		return false;

	_boneFinalMatrix->PushGraphicsData(SRV_REGISTER::t7);
	return true;
}

void Animator::PushData()
{
	if (_bones == nullptr || _animClips == nullptr || _animClips->empty())
		return;

	shared_ptr<Mesh> mesh = GetGameObject()->GetMeshRenderer()->GetMesh();
	if (mesh == nullptr || mesh->IsAnimMesh() == false)
		return;

	const uint32 boneCount = static_cast<uint32>(_bones->size());
	if (_boneFinalMatrix->GetElementCount() < boneCount)
		_boneFinalMatrix->Init(sizeof(Matrix), boneCount);

	mesh->GetBoneFrameDataBuffer()->PushComputeSRVData(SRV_REGISTER::t8);
	mesh->GetBoneOffsetBuffer()->PushComputeSRVData(SRV_REGISTER::t9);

	// 본 프레임이 부모 기준 로컬이면(.bin) 계층을 다시 조립해야 하므로
	// 부모 인덱스 테이블을 함께 넘긴다. FBX 는 이미 모델 공간이라 필요 없다.
	const bool composeHierarchy =
		mesh->AreBoneFramesLocal() && mesh->GetBoneParentBuffer() != nullptr;

	if (composeHierarchy)
		mesh->GetBoneParentBuffer()->PushComputeSRVData(SRV_REGISTER::t10);

	// 상체 레이어가 꺼져 있으면 마스크를 묶지 않는다. 셰이더는 레이어 세기가
	// 0 이면 마스크를 아예 읽지 않는다.
	const bool useLayer = (_boneMask != nullptr && _layerWeight > 0.001f);
	if (useLayer)
		_boneMask->PushComputeSRVData(SRV_REGISTER::t11);

	_boneFinalMatrix->PushComputeUAVData(UAV_REGISTER::u0);

	// 페이드 중이 아니면 두 채널을 같은 값으로 채운다. 셰이더가 분기 없이
	// 늘 같은 경로를 타게 하기 위함이다.
	const AnimChannel& baseA = _base.IsFading() ? _base.previous : _base.current;
	const AnimChannel& upperA = _upper.IsFading() ? _upper.previous : _upper.current;

	_computeMaterial->SetInt(0, boneCount);
	_computeMaterial->SetInt(3, composeHierarchy ? 1 : 0);

	_computeMaterial->SetVec4(0, PackChannel(baseA));
	_computeMaterial->SetVec4(1, PackChannel(_base.current));
	_computeMaterial->SetVec4(2, PackChannel(upperA));
	_computeMaterial->SetVec4(3, PackChannel(_upper.current));

	_computeMaterial->SetFloat(0, _base.BlendWeight());
	_computeMaterial->SetFloat(1, _upper.BlendWeight());
	_computeMaterial->SetFloat(2, useLayer ? _layerWeight : 0.f);

	const uint32 groupCount = (boneCount / 256) + 1;
	_computeMaterial->Dispatch(groupCount, 1, 1);

	// Graphics Shader
	_boneFinalMatrix->PushGraphicsData(SRV_REGISTER::t7);
}
