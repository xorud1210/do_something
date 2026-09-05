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

	// 채널은 매 프레임 내용이 바뀐다. UPLOAD 힙에 한 번 만들어두고 덮어쓴다.
	_channelBuffer = make_shared<StructuredBuffer>();
	_channelBuffer->InitDynamic(sizeof(AnimChannelData), ANIM_MAX_CHANNELS);

	_channelScratch.reserve(ANIM_MAX_CHANNELS);
}

Animator::~Animator()
{
}

void Animator::SetAnimClip(const vector<AnimClipInfo>* animClips)
{
	_animClips = animClips;

	// 아무도 Play 를 부르지 않아도 첫 클립이 돌게 한다.
	// 예전 구조는 채널의 clipIndex 기본값이 0 이라 저절로 그렇게 됐는데,
	// 포즈는 샘플이 0 개로 시작하므로 채널이 하나도 안 나가고,
	// 그러면 본 행렬 버퍼를 아무도 채우지 않아 메시가 한 점으로 뭉개진다.
	if (_animClips != nullptr && _animClips->empty() == false
		&& _base.current.sampleCount == 0)
	{
		_base.current.SetSingle(0, true);
	}
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

bool Animator::StartPose(AnimLayer& layer, const AnimPose& pose, float fadeDuration)
{
	if (pose.sampleCount <= 0)
		return false;

	// 페이드는 이전 포즈가 있을 때만 의미가 있다.
	if (fadeDuration > 0.f && layer.current.sampleCount > 0)
	{
		layer.previous = layer.current;
		layer.fadeElapsed = 0.f;
		layer.fadeDuration = fadeDuration;
	}
	else
	{
		layer.fadeDuration = 0.f;
	}

	layer.current = pose;
	layer.finished = false;
	return true;
}

void Animator::Play(uint32 idx, float fadeDuration, bool loop)
{
	if (_animClips == nullptr || idx >= _animClips->size())
		return;

	// 같은 클립을 다시 요청하면 무시한다. 그렇지 않으면 입력이 들어오는 매 프레임
	// 재생 위치가 0 으로 되감겨 애니메이션이 멈춘 것처럼 보인다.
	if (_baseIsBlendSpace == false
		&& _base.current.IsSingle(static_cast<int32>(idx))
		&& _base.IsFading() == false)
		return;

	AnimPose pose;
	pose.SetSingle(static_cast<int32>(idx), loop);

	if (StartPose(_base, pose, fadeDuration))
		_baseIsBlendSpace = false;
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

	AnimPose pose;
	pose.SetSingle(idx, loop);

	if (StartPose(_upper, pose, fadeDuration) == false)
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
// 2D 블렌드 스페이스
// ---------------------------------------------------------------------------

bool Animator::BuildBlendSpace2D(const vector<pair<Vec2, wstring>>& nodes)
{
	_blendSpace.clear();

	for (const pair<Vec2, wstring>& node : nodes)
	{
		const int32 idx = FindClip(node.second);
		if (idx < 0)
			continue;		// 이 모델에 없는 클립은 조용히 건너뛴다

		BlendSpaceNode entry;
		entry.position = node.first;
		entry.clipIndex = idx;
		_blendSpace.push_back(entry);
	}

	return _blendSpace.empty() == false;
}

// 파라미터 좌표에서 가까운 노드들을 골라 가중치를 매긴다.
//
// 거리 제곱의 역수를 쓴다. 노드 위에 정확히 서면 그 노드의 가중치가 사실상 1 이 되고,
// 사이에 있으면 양쪽이 비율대로 섞인다.
// 가까운 몇 개만 쓰는 이유는, 전부 섞으면 반대편 클립(뒤로 걷기 등)까지
// 조금씩 들어와 자세가 뭉개지기 때문이다.
void Animator::EvaluateBlendSpace(AnimPose& pose) const
{
	const int32 nodeCount = static_cast<int32>(_blendSpace.size());
	if (nodeCount <= 0)
		return;

	int32 useCount = (nodeCount < 4) ? nodeCount : 4;
	if (useCount > ANIM_MAX_SAMPLES)
		useCount = ANIM_MAX_SAMPLES;

	// 노드가 열 개 안팎이라 정렬 대신 필요한 개수만 골라낸다.
	vector<float> distSq(nodeCount);
	for (int32 i = 0; i < nodeCount; i++)
	{
		const Vec2 diff = _blendParam - _blendSpace[i].position;
		distSq[i] = diff.LengthSquared();
	}

	vector<bool> taken(nodeCount, false);
	float total = 0.f;
	int32 picked = 0;

	for (int32 k = 0; k < useCount; k++)
	{
		int32 best = -1;
		for (int32 i = 0; i < nodeCount; i++)
		{
			if (taken[i])
				continue;
			if (best < 0 || distSq[i] < distSq[best])
				best = i;
		}
		if (best < 0)
			break;

		taken[best] = true;

		// 거리가 0 일 때 무한대가 되지 않게 아주 작은 값을 더한다.
		const float weight = 1.f / (distSq[best] + 0.0001f);

		pose.samples[picked].clipIndex = _blendSpace[best].clipIndex;
		pose.samples[picked].weight = weight;
		total += weight;
		picked++;
	}

	if (total <= 0.f)
		return;

	for (int32 i = 0; i < picked; i++)
		pose.samples[i].weight /= total;

	pose.sampleCount = picked;
}

bool Animator::PlayBlendSpace(float fadeDuration)
{
	if (_blendSpace.empty())
		return false;

	// 이미 블렌드 스페이스면 좌표만 갱신하면 된다. 다시 걸면 위상이 0 으로 되감긴다.
	if (_baseIsBlendSpace && _base.IsFading() == false)
		return true;

	AnimPose pose;
	pose.loop = true;
	pose.phase = _base.current.phase;	// 전환해도 발이 튀지 않게 위상을 이어받는다
	EvaluateBlendSpace(pose);

	if (StartPose(_base, pose, fadeDuration) == false)
		return false;

	_baseIsBlendSpace = true;
	return true;
}

void Animator::SetBlendParam(const Vec2& param)
{
	_blendParam = param;

	// 전환이 아니라 가중치만 다시 계산한다. 위상은 그대로 둔다.
	if (_baseIsBlendSpace)
		EvaluateBlendSpace(_base.current);
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

bool Animator::AdvancePose(AnimPose& pose, float deltaTime)
{
	if (pose.sampleCount <= 0 || _animClips == nullptr)
		return false;

	// 재생 속도를 가중 평균으로 정한다.
	// 걷기(1.2초)와 달리기(0.7초)를 반씩 섞으면 그 사이 속도로 돈다.
	// 이렇게 하지 않으면 두 클립의 발 접지 타이밍이 어긋나 다리가 떤다.
	float rate = 0.f;
	float totalWeight = 0.f;

	for (int32 i = 0; i < pose.sampleCount; i++)
	{
		const AnimSample& sample = pose.samples[i];
		if (sample.weight <= 0.f)
			continue;

		const AnimClipInfo& clip = _animClips->at(sample.clipIndex);
		if (clip.duration <= 0.0 || clip.frameCount <= 0)
			continue;

		rate += sample.weight / static_cast<float>(clip.duration);
		totalWeight += sample.weight;
	}

	if (totalWeight <= 0.f || rate <= 0.f)
		return false;

	rate /= totalWeight;
	pose.phase += rate * deltaTime;

	bool finished = false;
	if (pose.phase >= 1.f)
	{
		if (pose.loop)
			pose.phase = std::fmod(pose.phase, 1.f);
		else
		{
			pose.phase = 1.f;
			finished = true;
		}
	}

	return finished;
}

void Animator::UpdateLayer(AnimLayer& layer, float deltaTime)
{
	layer.finished = AdvancePose(layer.current, deltaTime);

	if (layer.IsFading())
	{
		// 빠져나가는 포즈도 계속 돌려야 섞이는 동안 어색하지 않다.
		AdvancePose(layer.previous, deltaTime);

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

AnimChannelData Animator::PackSample(const AnimSample& sample, float phase, bool loop, float weight)
{
	AnimChannelData data = {};

	shared_ptr<Mesh> mesh = GetGameObject()->GetMeshRenderer()->GetMesh();
	const AnimClipInfo& clip = _animClips->at(sample.clipIndex);
	const int32 frameCount = clip.frameCount;

	// 위상은 0~1 이고, 클립마다 프레임 수가 다르다.
	// 같은 위상이 각 클립의 같은 지점(예: 오른발이 땅에 닿는 순간)을 가리킨다.
	const float exact = std::clamp(phase, 0.f, 1.f) * static_cast<float>(frameCount);

	int32 frame = static_cast<int32>(exact);
	if (frame > frameCount - 1)
		frame = frameCount - 1;
	if (frame < 0)
		frame = 0;

	// 루프 클립은 마지막 프레임에서 0 번으로 이어져야 이음매가 생기지 않는다.
	int32 nextFrame;
	if (frame >= frameCount - 1)
		nextFrame = loop ? 0 : frameCount - 1;
	else
		nextFrame = frame + 1;

	data.frame = Vec4(
		static_cast<float>(mesh->GetClipFrameOffset(sample.clipIndex)),
		static_cast<float>(frame),
		static_cast<float>(nextFrame),
		std::clamp(exact - static_cast<float>(frame), 0.f, 1.f));
	data.weight = weight;

	return data;
}

int32 Animator::AppendChannels(const AnimLayer& layer, vector<AnimChannelData>& out)
{
	const float blend = layer.BlendWeight();
	int32 added = 0;

	// 페이드 중이면 빠져나가는 포즈도 같이 넣는다.
	// 레이어 안의 크로스페이드를 채널 가중치에 미리 곱해두면,
	// 셰이더는 "가중치대로 더하기" 하나만 하면 된다.
	if (layer.IsFading() && blend < 1.f)
	{
		for (int32 i = 0; i < layer.previous.sampleCount; i++)
		{
			const AnimSample& sample = layer.previous.samples[i];
			if (sample.weight <= 0.f)
				continue;
			if (out.size() >= ANIM_MAX_CHANNELS)
				break;

			out.push_back(PackSample(sample, layer.previous.phase, layer.previous.loop,
				sample.weight * (1.f - blend)));
			added++;
		}
	}

	const float currentScale = layer.IsFading() ? blend : 1.f;

	for (int32 i = 0; i < layer.current.sampleCount; i++)
	{
		const AnimSample& sample = layer.current.samples[i];
		if (sample.weight <= 0.f)
			continue;
		if (out.size() >= ANIM_MAX_CHANNELS)
			break;

		out.push_back(PackSample(sample, layer.current.phase, layer.current.loop,
			sample.weight * currentScale));
		added++;
	}

	return added;
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

	// 이번 프레임에 섞을 채널들을 모은다.
	// 기본 레이어가 먼저, 상체 레이어가 그 뒤에 붙는다.
	_channelScratch.clear();
	const int32 baseCount = AppendChannels(_base, _channelScratch);
	const int32 upperCount = useLayer ? AppendChannels(_upper, _channelScratch) : 0;

	if (_channelScratch.empty())
		return;

	// 채널 수는 매 프레임 달라지지만 버퍼는 상한 크기 그대로다.
	// 몇 개를 읽을지는 g_int_1 / g_int_2 로 알려준다.
	_channelBuffer->UpdateDynamic(_channelScratch.data(),
		static_cast<uint32>(_channelScratch.size()));
	_channelBuffer->PushComputeSRVData(SRV_REGISTER::t12);

	_boneFinalMatrix->PushComputeUAVData(UAV_REGISTER::u0);

	_computeMaterial->SetInt(0, boneCount);
	_computeMaterial->SetInt(1, baseCount);
	_computeMaterial->SetInt(2, upperCount);
	_computeMaterial->SetInt(3, composeHierarchy ? 1 : 0);

	_computeMaterial->SetFloat(2, useLayer ? _layerWeight : 0.f);

	const uint32 groupCount = (boneCount / 256) + 1;
	_computeMaterial->Dispatch(groupCount, 1, 1);

	// Graphics Shader
	_boneFinalMatrix->PushGraphicsData(SRV_REGISTER::t7);
}
