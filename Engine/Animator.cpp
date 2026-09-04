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

bool Animator::PlayByName(const wstring& name, float fadeDuration, bool loop)
{
	const int32 idx = FindClip(name);
	if (idx < 0)
		return false;

	Play(static_cast<uint32>(idx), fadeDuration, loop);
	return true;
}

void Animator::Play(uint32 idx, float fadeDuration, bool loop)
{
	if (_animClips == nullptr || idx >= _animClips->size())
		return;

	// 같은 클립을 다시 요청하면 무시한다. 그렇지 않으면 입력이 들어오는 매 프레임
	// 재생 위치가 0 으로 되감겨 애니메이션이 멈춘 것처럼 보인다.
	if (static_cast<int32>(idx) == _current.clipIndex && _fadeDuration <= 0.f)
		return;

	if (fadeDuration > 0.f)
	{
		_previous = _current;
		_fadeElapsed = 0.f;
		_fadeDuration = fadeDuration;
	}
	else
	{
		_fadeDuration = 0.f;
	}

	_current.clipIndex = static_cast<int32>(idx);
	_current.updateTime = 0.f;
	_current.frame = 0;
	_current.nextFrame = 0;
	_current.frameRatio = 0.f;
	_current.loop = loop;
	_currentFinished = false;
}

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
	// (원래는 _frame - _frame 이라 항상 0 이었고 보간이 전혀 되지 않았다)
	channel.frameRatio = std::clamp(exactFrame - static_cast<float>(channel.frame), 0.f, 1.f);

	return finished;
}

void Animator::FinalUpdate()
{
	if (_animClips == nullptr || _animClips->empty())
		return;

	const float deltaTime = DELTA_TIME;

	_currentFinished = AdvanceChannel(_current, deltaTime);

	if (_fadeDuration > 0.f)
	{
		// 빠져나가는 클립도 계속 돌려야 섞이는 동안 어색하지 않다.
		AdvanceChannel(_previous, deltaTime);

		_fadeElapsed += deltaTime;
		if (_fadeElapsed >= _fadeDuration)
			_fadeDuration = 0.f;
	}
}

Vec4 Animator::PackChannel(const AnimChannel& channel)
{
	shared_ptr<Mesh> mesh = GetGameObject()->GetMeshRenderer()->GetMesh();
	const float clipOffset = static_cast<float>(mesh->GetClipFrameOffset(channel.clipIndex));

	return Vec4(clipOffset,
		static_cast<float>(channel.frame),
		static_cast<float>(channel.nextFrame),
		channel.frameRatio);
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

	_boneFinalMatrix->PushComputeUAVData(UAV_REGISTER::u0);

	// 페이드 중이 아니면 두 채널을 같은 값으로 채우고 가중치를 1 로 둔다.
	// 셰이더가 분기 없이 늘 같은 경로를 타게 하기 위함이다.
	const bool fading = (_fadeDuration > 0.f);
	const float blendWeight = fading
		? std::clamp(_fadeElapsed / _fadeDuration, 0.f, 1.f)
		: 1.f;

	_computeMaterial->SetInt(0, boneCount);
	_computeMaterial->SetInt(3, composeHierarchy ? 1 : 0);
	_computeMaterial->SetVec4(0, PackChannel(fading ? _previous : _current));
	_computeMaterial->SetVec4(1, PackChannel(_current));
	_computeMaterial->SetFloat(0, blendWeight);

	const uint32 groupCount = (boneCount / 256) + 1;
	_computeMaterial->Dispatch(groupCount, 1, 1);

	// Graphics Shader
	_boneFinalMatrix->PushGraphicsData(SRV_REGISTER::t7);
}
