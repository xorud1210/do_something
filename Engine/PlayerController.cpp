#include "pch.h"
#include "PlayerController.h"
#include "GameObject.h"
#include "Transform.h"
#include "Animator.h"
#include "Input.h"
#include "Timer.h"

// Player.bin 의 클립 이름. 익스포터가 이름을 살려줘서 인덱스 대신 이름으로 찾는다.
static const wstring CLIP_IDLE = L"idle";
static const wstring CLIP_RUN = L"Run_Forward";
static const wstring CLIP_ATTACK = L"Combat_1H_Attack";

// 상체 레이어의 시작 본. 이 본 이하가 상체다.
static const wstring UPPER_BODY_ROOT = L"spine_01";

PlayerController::PlayerController()
{
}

PlayerController::~PlayerController()
{
}

void PlayerController::AddPart(shared_ptr<GameObject> part)
{
	if (part == nullptr)
		return;

	_parts.push_back(part);

	// 상체 마스크는 스켈레톤이 정해지는 시점(= 파츠가 붙는 시점)에 만든다.
	shared_ptr<Animator> animator = part->GetAnimator();
	if (animator)
		animator->BuildUpperBodyMask(UPPER_BODY_ROOT);
}

void PlayerController::PlayAll(const wstring& clipName, float fade, bool loop)
{
	if (clipName == _currentClip)
		return;

	bool played = false;
	for (weak_ptr<GameObject>& weak : _parts)
	{
		shared_ptr<GameObject> part = weak.lock();
		if (part == nullptr)
			continue;

		shared_ptr<Animator> animator = part->GetAnimator();
		if (animator && animator->PlayByName(clipName, fade, loop))
			played = true;
	}

	// 클립 이름이 없는 모델이면 아무것도 하지 않는다.
	if (played)
		_currentClip = clipName;
}

void PlayerController::PlayUpperAll(const wstring& clipName, float fade, bool loop)
{
	for (weak_ptr<GameObject>& weak : _parts)
	{
		shared_ptr<GameObject> part = weak.lock();
		if (part == nullptr)
			continue;

		shared_ptr<Animator> animator = part->GetAnimator();
		if (animator)
			animator->PlayUpperLayer(clipName, fade, loop);
	}
}

void PlayerController::StopUpperAll(float fade)
{
	for (weak_ptr<GameObject>& weak : _parts)
	{
		shared_ptr<GameObject> part = weak.lock();
		if (part == nullptr)
			continue;

		shared_ptr<Animator> animator = part->GetAnimator();
		if (animator)
			animator->StopUpperLayer(fade);
	}
}

bool PlayerController::UpperFinished() const
{
	for (const weak_ptr<GameObject>& weak : _parts)
	{
		shared_ptr<GameObject> part = weak.lock();
		if (part == nullptr)
			continue;

		shared_ptr<Animator> animator = part->GetAnimator();
		if (animator && animator->IsUpperFinished())
			return true;
	}
	return false;
}

void PlayerController::LateUpdate()
{
	shared_ptr<Transform> transform = GetTransform();
	if (transform == nullptr)
		return;

	const float deltaTime = DELTA_TIME;

	// --- 입력 ---
	Vec3 move = Vec3(0.f, 0.f, 0.f);
	if (INPUT->GetButton(KEY_TYPE::W)) move.z += 1.f;
	if (INPUT->GetButton(KEY_TYPE::S)) move.z -= 1.f;
	if (INPUT->GetButton(KEY_TYPE::A)) move.x -= 1.f;
	if (INPUT->GetButton(KEY_TYPE::D)) move.x += 1.f;

	const bool moving = (move.LengthSquared() > 0.f);

	// --- 공격: 상체 레이어 ---
	// 하체는 그대로 달리거나 서 있고, 상체만 공격 모션을 덮어쓴다.
	// 마스크가 spine_01 이하에만 걸려 있어서 "달리면서 공격"이 나온다.
	if (_attacking && UpperFinished())
	{
		_attacking = false;
		StopUpperAll(0.2f);
	}

	if (_attacking == false && INPUT->GetButtonDown(KEY_TYPE::LBUTTON))
	{
		_attacking = true;
		PlayUpperAll(CLIP_ATTACK, 0.12f, false);
	}

	// --- 이동 ---
	if (moving)
	{
		move.Normalize();
		transform->SetLocalPosition(transform->GetLocalPosition() + move * _moveSpeed * deltaTime);

		// 이동 방향으로 부드럽게 돌린다. 최단 회전이 되도록 각도 차이를 -PI~PI 로 감는다.
		const float targetYaw = ::atan2f(move.x, move.z);
		float diff = targetYaw - _yaw;
		while (diff > XM_PI)  diff -= XM_2PI;
		while (diff < -XM_PI) diff += XM_2PI;

		_yaw += diff * std::clamp(_turnSpeed * deltaTime, 0.f, 1.f);
		transform->SetLocalRotation(Vec3(0.f, _yaw, 0.f));
	}

	// --- 하체(기본 레이어)는 이동 여부만 본다 ---
	PlayAll(moving ? CLIP_RUN : CLIP_IDLE, 0.25f, true);
}
