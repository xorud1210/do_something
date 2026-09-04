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

PlayerController::PlayerController()
{
}

PlayerController::~PlayerController()
{
}

void PlayerController::AddPart(shared_ptr<GameObject> part)
{
	if (part)
		_parts.push_back(part);
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

bool PlayerController::AnyPartFinished() const
{
	for (const weak_ptr<GameObject>& weak : _parts)
	{
		shared_ptr<GameObject> part = weak.lock();
		if (part == nullptr)
			continue;

		shared_ptr<Animator> animator = part->GetAnimator();
		if (animator && animator->IsFinished())
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

	// --- 공격 ---
	// 비루프 클립이라 끝나면 스스로 이동/대기 상태로 돌아온다.
	if (_attacking && AnyPartFinished())
		_attacking = false;

	if (_attacking == false && INPUT->GetButtonDown(KEY_TYPE::LBUTTON))
	{
		_attacking = true;
		_currentClip.clear();				// 같은 클립 재요청 무시를 우회
		PlayAll(CLIP_ATTACK, 0.1f, false);
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

	// --- 애니메이션 ---
	if (_attacking == false)
		PlayAll(moving ? CLIP_RUN : CLIP_IDLE, 0.25f, true);
}
