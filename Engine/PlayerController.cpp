#include "pch.h"
#include "PlayerController.h"
#include "GameObject.h"
#include "Transform.h"
#include "Animator.h"
#include "Input.h"
#include "Timer.h"

// Player.bin 의 클립 이름. 익스포터가 이름을 살려줘서 인덱스 대신 이름으로 찾는다.
static const wstring CLIP_IDLE = L"idle";
static const wstring CLIP_ATTACK = L"Combat_1H_Attack";

// 상체 레이어의 시작 본. 이 본 이하가 상체다.
static const wstring UPPER_BODY_ROOT = L"spine_01";

PlayerController::PlayerController()
{
}

PlayerController::~PlayerController()
{
}

// 2D 블렌드 스페이스의 배치.
//
//   x = 좌우 이동 성분 (-1 왼쪽, +1 오른쪽)
//   y = 앞뒤 이동 성분 (-1 뒤,   +1 앞)
//   원점에서의 거리 = 속도. 0 이면 정지, 0.5 가 걷기, 1 이 달리기.
//
// 그래서 정지가 가운데, 걷기가 안쪽 고리, 달리기가 바깥 고리에 놓인다.
void PlayerController::BuildBlendSpace(shared_ptr<Animator> animator)
{
	vector<pair<Vec2, wstring>> nodes =
	{
		{ Vec2( 0.0f,  0.0f), CLIP_IDLE },

		{ Vec2( 0.0f,  0.5f), L"Walk_Forward"   },
		{ Vec2( 0.0f, -0.5f), L"Walk_Backwards" },
		{ Vec2(-0.5f,  0.0f), L"Walk_Left"      },
		{ Vec2( 0.5f,  0.0f), L"Walk_Right"     },

		{ Vec2( 0.0f,  1.0f), L"Run_Forward"    },
		{ Vec2( 0.0f, -1.0f), L"Run_Backwards"  },
		{ Vec2(-1.0f,  0.0f), L"Run_Left"       },
		{ Vec2( 1.0f,  0.0f), L"Run_Right"      },
	};

	if (animator->BuildBlendSpace2D(nodes) == false)
		return;

	animator->PlayBlendSpace(0.f);
	_hasBlendSpace = true;
}

void PlayerController::AddPart(shared_ptr<GameObject> part)
{
	if (part == nullptr)
		return;

	_parts.push_back(part);

	shared_ptr<Animator> animator = part->GetAnimator();
	if (animator == nullptr)
		return;

	// 상체 마스크와 블렌드 스페이스 모두 스켈레톤과 클립 목록이 정해지는 시점
	// (= 파츠가 붙는 시점)에 만든다.
	animator->BuildUpperBodyMask(UPPER_BODY_ROOT);
	BuildBlendSpace(animator);
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

void PlayerController::SetBlendParamAll(const Vec2& param)
{
	for (weak_ptr<GameObject>& weak : _parts)
	{
		shared_ptr<GameObject> part = weak.lock();
		if (part == nullptr)
			continue;

		shared_ptr<Animator> animator = part->GetAnimator();
		if (animator)
			animator->SetBlendParam(param);
	}
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
	const bool walking = INPUT->GetButton(KEY_TYPE::LSHIFT);

	// 시선 고정. 바라보는 방향을 유지한 채 옆이나 뒤로 움직인다.
	// 이때 비로소 "이동 방향"과 "바라보는 방향"이 갈라진다.
	// 블렌드 스페이스가 의미를 갖는 건 이 상태다 -
	// 그냥 걸을 때는 몸이 이동 방향으로 돌아가므로 앞으로 가는 클립 하나면 충분하다.
	const bool facingLocked = INPUT->GetButton(KEY_TYPE::Q);

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
	const float speedScale = walking ? 0.5f : 1.f;

	if (moving)
	{
		move.Normalize();
		transform->SetLocalPosition(
			transform->GetLocalPosition() + move * _moveSpeed * speedScale * deltaTime);

		if (facingLocked == false)
		{
			// 이동 방향으로 부드럽게 돌린다. 최단 회전이 되도록 각도 차이를 -PI~PI 로 감는다.
			const float targetYaw = ::atan2f(move.x, move.z);
			float diff = targetYaw - _yaw;
			while (diff > XM_PI)  diff -= XM_2PI;
			while (diff < -XM_PI) diff += XM_2PI;

			_yaw += diff * std::clamp(_turnSpeed * deltaTime, 0.f, 1.f);
			transform->SetLocalRotation(Vec3(0.f, _yaw, 0.f));
		}
	}

	// --- 블렌드 좌표 ---
	if (_hasBlendSpace)
	{
		Vec2 target = Vec2(0.f, 0.f);

		if (moving)
		{
			// 월드 이동 방향을 캐릭터 기준으로 옮긴다.
			// 캐릭터의 앞은 (sin yaw, 0, cos yaw), 오른쪽은 (cos yaw, 0, -sin yaw) 다.
			const float sinY = ::sinf(_yaw);
			const float cosY = ::cosf(_yaw);

			const float forward = move.x * sinY + move.z * cosY;
			const float right = move.x * cosY - move.z * sinY;

			target = Vec2(right, forward) * speedScale;
		}

		// 입력이 끊기는 순간 좌표가 튀면 클립도 튄다. 목표값을 따라가게 한다.
		const float t = std::clamp(_blendFollowSpeed * deltaTime, 0.f, 1.f);
		_blendParam += (target - _blendParam) * t;

		SetBlendParamAll(_blendParam);
	}
	else
	{
		// 블렌드 스페이스를 못 만든 모델은 예전처럼 클립 하나로 간다.
		PlayAll(moving ? L"Run_Forward" : CLIP_IDLE, 0.25f, true);
	}
}
