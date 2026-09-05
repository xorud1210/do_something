#pragma once
#include "Component.h"
#include "Mesh.h"

class Material;
class StructuredBuffer;
class Mesh;

// 한 클립을 재생 중인 채널. 크로스페이드는 두 채널을 겹쳐서 섞는다.
struct AnimChannel
{
	int32	clipIndex = 0;
	float	updateTime = 0.f;
	int32	frame = 0;
	int32	nextFrame = 0;
	float	frameRatio = 0.f;
	bool	loop = true;
};

// 레이어 하나 = 크로스페이드 중인 채널 두 개.
struct AnimLayer
{
	AnimChannel	current;
	AnimChannel	previous;
	float		fadeElapsed = 0.f;
	float		fadeDuration = 0.f;		// 0 이면 페이드 중이 아님
	bool		finished = false;		// 비루프 클립이 끝까지 갔는가

	bool IsFading() const { return fadeDuration > 0.f; }
	float BlendWeight() const
	{
		return IsFading() ? std::clamp(fadeElapsed / fadeDuration, 0.f, 1.f) : 1.f;
	}
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
	// 한 프레임 전 포즈를 쓰게 되지만 눈에 띄지 않는다.
	bool PushBoneData();

	int32 GetAnimCount() { return _animClips ? static_cast<int32>(_animClips->size()) : 0; }
	int32 GetCurrentClipIndex() { return _base.current.clipIndex; }

	// --- 기본 레이어 (전신) ---
	// fadeDuration 이 0 이면 즉시 전환, 그보다 크면 그 시간 동안 이전 클립과 섞는다.
	// 이미 같은 클립을 재생 중이면 아무것도 하지 않는다(연타로 리셋되는 것 방지).
	void Play(uint32 idx, float fadeDuration = 0.2f, bool loop = true);
	bool PlayByName(const wstring& name, float fadeDuration = 0.2f, bool loop = true);
	bool IsFinished() const { return _base.finished; }

	// --- 상체 레이어 ---
	// 마스크가 1 인 본만 이 레이어의 포즈를 따른다. 하체는 기본 레이어 그대로다.
	// 달리면서 공격 같은 동작이 이걸로 만들어진다.
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
	// 채널 하나를 진행. 루프가 아니고 끝에 도달하면 true.
	bool AdvanceChannel(AnimChannel& channel, float deltaTime);
	// 레이어에 클립을 건다.
	bool StartClip(AnimLayer& layer, int32 clipIndex, float fadeDuration, bool loop);

	// 셰이더에 넘길 (클립오프셋, 프레임, 다음프레임, 보간비율) 묶음.
	Vec4 PackChannel(const AnimChannel& channel);

private:
	const vector<BoneInfo>* _bones = nullptr;
	const vector<AnimClipInfo>* _animClips = nullptr;

	AnimLayer						_base;		// 전신
	AnimLayer						_upper;		// 상체만

	// 상체 레이어의 전체 세기. 켜고 끌 때 부드럽게 오르내린다.
	float							_layerWeight = 0.f;
	float							_layerTarget = 0.f;
	float							_layerFadeSpeed = 0.f;	// 초당 변화량

	shared_ptr<StructuredBuffer>	_boneMask;	// 본별 상체 가중치
	shared_ptr<Material>			_computeMaterial;
	shared_ptr<StructuredBuffer>	_boneFinalMatrix;
};
