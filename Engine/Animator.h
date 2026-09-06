#pragma once
#include "Component.h"
#include "Mesh.h"

class Material;
class StructuredBuffer;
class Mesh;

enum
{
	// 포즈 하나가 섞을 수 있는 클립 수.
	ANIM_MAX_SAMPLES = 8,
	// 컴퓨트 셰이더로 넘기는 채널 수의 상한.
	// 기본 레이어가 페이드 중이면 (현재 + 이전) 두 포즈가 다 들어가고,
	// 여기에 상체 레이어가 더 붙는다.
	ANIM_MAX_CHANNELS = 24,
};

// 포즈 하나를 만드는 데 쓰이는 클립과 그 가중치.
struct AnimSample
{
	int32	clipIndex = 0;
	float	weight = 0.f;
};

// 여러 클립을 같은 위상에서 읽어 하나로 섞은 자세.
// 클립이 하나뿐이면 일반 재생과 같다. 블렌드 스페이스는 여러 개가 들어온다.
//
// 위상(0~1)을 공유하는 것이 핵심이다.
// 클립마다 자기 시간으로 돌리면 걷기의 발이 땅에 있을 때 달리기의 발은
// 공중이라, 섞는 순간 다리가 부들부들 떤다.
struct AnimPose
{
	array<AnimSample, ANIM_MAX_SAMPLES>	samples = {};
	int32	sampleCount = 0;
	float	phase = 0.f;
	bool	loop = true;

	void SetSingle(int32 clipIndex, bool loopClip)
	{
		samples[0].clipIndex = clipIndex;
		samples[0].weight = 1.f;
		sampleCount = 1;
		phase = 0.f;
		loop = loopClip;
	}

	bool IsSingle(int32 clipIndex) const
	{
		return sampleCount == 1 && samples[0].clipIndex == clipIndex;
	}
};

// 레이어 하나 = 크로스페이드 중인 두 포즈.
struct AnimLayer
{
	AnimPose	current;
	AnimPose	previous;
	float		fadeElapsed = 0.f;
	float		fadeDuration = 0.f;		// 0 이면 페이드 중이 아님
	bool		finished = false;		// 비루프 포즈가 끝까지 갔는가

	bool IsFading() const { return fadeDuration > 0.f; }
	float BlendWeight() const
	{
		return IsFading() ? std::clamp(fadeElapsed / fadeDuration, 0.f, 1.f) : 1.f;
	}
};

// 2D 블렌드 스페이스의 노드 하나.
// 평면 위의 좌표에 클립을 하나 못박아 둔다.
struct BlendSpaceNode
{
	Vec2	position;
	int32	clipIndex = 0;
};

// 컴퓨트 셰이더로 넘길 채널 하나.
// animation.fx 의 AnimChannel 과 배치가 같아야 한다.
struct AnimChannelData
{
	Vec4	frame;		// (클립 시작 오프셋, 프레임, 다음 프레임, 보간비율)
	float	weight;
	Vec3	padding;
};

class Animator : public Component
{
public:
	Animator();
	virtual ~Animator();

public:
	void SetBones(const vector<BoneInfo>* bones) { _bones = bones; }
	void SetAnimClip(const vector<AnimClipInfo>* animClips);
	void PushData();

	// 이미 계산해 둔 본 행렬을 그래픽스 파이프라인에 다시 묶기만 한다.
	// 셰도우 패스는 디퍼드보다 먼저 도는데, 여기서 컴퓨트를 또 돌릴 이유는 없다.
	//
	// 예전에는 한 프레임 전 포즈를 쓰게 됐다. 지금은 이번 프레임 컴퓨트가
	// 그래픽스 커맨드 리스트 전체보다 먼저 실행되므로 이번 프레임 포즈다.
	// 커맨드 리스트 안의 기록 순서와 GPU 의 실행 순서는 다른 이야기다.
	bool PushBoneData();

	int32 GetAnimCount() { return _animClips ? static_cast<int32>(_animClips->size()) : 0; }

	// --- 기본 레이어 (전신) ---
	// fadeDuration 이 0 이면 즉시 전환, 그보다 크면 그 시간 동안 이전 포즈와 섞는다.
	void Play(uint32 idx, float fadeDuration = 0.2f, bool loop = true);
	bool PlayByName(const wstring& name, float fadeDuration = 0.2f, bool loop = true);
	bool IsFinished() const { return _base.finished; }

	// --- 2D 블렌드 스페이스 ---
	// 평면 위에 클립을 배치해두고, 입력 좌표 주변의 클립들을 거리 가중치로 섞는다.
	// 방향(각도)과 속도(원점에서의 거리)를 한 좌표로 다룬다.
	bool BuildBlendSpace2D(const vector<pair<Vec2, wstring>>& nodes);
	bool HasBlendSpace() const { return _blendSpace.empty() == false; }

	// 기본 레이어를 블렌드 스페이스로 전환한다.
	bool PlayBlendSpace(float fadeDuration = 0.25f);

	// 좌표만 갱신한다. 전환이 아니라 가중치만 다시 계산하므로 매 프레임 불러도 된다.
	void SetBlendParam(const Vec2& param);

	// --- 상체 레이어 ---
	// 마스크가 1 인 본만 이 레이어의 포즈를 따른다. 하체는 기본 레이어 그대로다.
	//
	// rootBoneName 이하 서브트리를 상체로 본다 (Player.bin 은 "spine_01").
	// featherBones 만큼은 경계에서 0 -> 1 로 서서히 올려, 허리가 뚝 끊기지 않게 한다.
	bool BuildUpperBodyMask(const wstring& rootBoneName, int32 featherBones = 2);

	bool PlayUpperLayer(const wstring& clipName, float fadeDuration = 0.15f, bool loop = false);
	void StopUpperLayer(float fadeDuration = 0.2f);

	bool IsUpperLayerActive() const { return _layerWeight > 0.001f; }
	bool IsUpperFinished() const { return _upper.finished; }

	int32 FindClip(const wstring& name) const;

public:
	virtual void FinalUpdate() override;

private:
	// 레이어 하나를 시간에 맞춰 진행시킨다.
	void UpdateLayer(AnimLayer& layer, float deltaTime);

	// 포즈를 진행. 루프가 아니고 끝에 도달하면 true.
	// 재생 속도는 섞고 있는 클립들의 가중 평균으로 정한다.
	bool AdvancePose(AnimPose& pose, float deltaTime);

	// 레이어에 포즈를 건다.
	bool StartPose(AnimLayer& layer, const AnimPose& pose, float fadeDuration);

	// 블렌드 파라미터로 샘플과 가중치를 다시 계산한다.
	void EvaluateBlendSpace(AnimPose& pose) const;

	// 샘플 하나를 셰이더가 읽을 채널로 바꾼다.
	AnimChannelData PackSample(const AnimSample& sample, float phase, bool loop, float weight);

	// 레이어의 채널들을 버퍼에 이어 붙인다. 넣은 개수를 돌려준다.
	int32 AppendChannels(const AnimLayer& layer, vector<AnimChannelData>& out);

private:
	const vector<BoneInfo>* _bones = nullptr;
	const vector<AnimClipInfo>* _animClips = nullptr;

	AnimLayer						_base;		// 전신
	AnimLayer						_upper;		// 상체만

	// 상체 레이어의 전체 세기. 켜고 끌 때 부드럽게 오르내린다.
	float							_layerWeight = 0.f;
	float							_layerTarget = 0.f;
	float							_layerFadeSpeed = 0.f;	// 초당 변화량

	// 2D 블렌드 스페이스
	vector<BlendSpaceNode>			_blendSpace;
	Vec2							_blendParam = Vec2(0.f, 0.f);
	bool							_baseIsBlendSpace = false;

	shared_ptr<StructuredBuffer>	_boneMask;		// 본별 상체 가중치
	shared_ptr<StructuredBuffer>	_channelBuffer;	// 이번 프레임의 채널들
	shared_ptr<Material>			_computeMaterial;
	shared_ptr<StructuredBuffer>	_boneFinalMatrix;

	vector<AnimChannelData>			_channelScratch;
};
