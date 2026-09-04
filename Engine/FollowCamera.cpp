#include "pch.h"
#include "FollowCamera.h"
#include "GameObject.h"
#include "Transform.h"
#include "Timer.h"

FollowCamera::FollowCamera()
{
}

FollowCamera::~FollowCamera()
{
}

void FollowCamera::LateUpdate()
{
	shared_ptr<GameObject> target = _target.lock();
	if (target == nullptr)
		return;

	shared_ptr<Transform> targetTransform = target->GetTransform();
	shared_ptr<Transform> transform = GetTransform();
	if (targetTransform == nullptr || transform == nullptr)
		return;

	const Vec3 targetPos = targetTransform->GetLocalPosition();
	const Vec3 desired = targetPos + _offset;

	Vec3 position = transform->GetLocalPosition();
	if (_snapped == false)
	{
		position = desired;
		_snapped = true;
	}
	else
	{
		// 지수 감쇠. 프레임레이트가 달라져도 따라붙는 느낌이 같도록 DELTA_TIME 을 쓴다.
		const float t = std::clamp(_damping * DELTA_TIME, 0.f, 1.f);
		position += (desired - position) * t;
	}

	transform->SetLocalPosition(position);

	Vec3 lookDir = (targetPos + Vec3(0.f, _lookHeight, 0.f)) - position;
	if (lookDir.LengthSquared() > 0.0001f)
		transform->LookAt(lookDir);
}
