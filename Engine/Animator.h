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

class Animator : public Component
{
public:
	Animator();
	virtual ~Animator();

public:
	void SetBones(const vector<BoneInfo>* bones) { _bones = bones; }
	void SetAnimClip(const vector<AnimClipInfo>* animClips);
	void PushData();

	int32 GetAnimCount() { return _animClips ? static_cast<int32>(_animClips->size()) : 0; }
	int32 GetCurrentClipIndex() { return _current.clipIndex; }

	// fadeDuration 이 0 이면 즉시 전환, 그보다 크면 그 시간 동안 이전 클립과 섞는다.
	// 이미 같은 클립을 재생 중이면 아무것도 하지 않는다(연타로 리셋되는 것 방지).
	void Play(uint32 idx, float fadeDuration = 0.2f, bool loop = true);

	// 클립 이름으로 찾기. 없으면 -1.
	int32 FindClip(const wstring& name) const;
	bool  PlayByName(const wstring& name, float fadeDuration = 0.2f, bool loop = true);

	// 현재 클립이 끝까지 재생됐는지(루프가 아닐 때만 의미 있다).
	bool IsFinished() const { return _currentFinished; }

public:
	virtual void FinalUpdate() override;

private:
	// 채널 하나를 시간에 맞춰 진행시킨다. 루프가 아니고 끝에 도달하면 true.
	bool AdvanceChannel(AnimChannel& channel, float deltaTime);

	// 셰이더에 넘길 (클립오프셋, 프레임, 다음프레임, 보간비율) 묶음.
	Vec4 PackChannel(const AnimChannel& channel);

private:
	const vector<BoneInfo>* _bones = nullptr;
	const vector<AnimClipInfo>* _animClips = nullptr;

	AnimChannel						_current;		// 지금 재생 중인 클립
	AnimChannel						_previous;		// 페이드 아웃 중인 이전 클립

	float							_fadeElapsed = 0.f;
	float							_fadeDuration = 0.f;	// 0 이면 페이드 없음
	bool							_currentFinished = false;

	shared_ptr<Material>			_computeMaterial;
	shared_ptr<StructuredBuffer>	_boneFinalMatrix;
};
