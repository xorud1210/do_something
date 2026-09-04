#pragma once
#include "MonoBehaviour.h"

class GameObject;

// 대상 뒤쪽 위에서 따라다니는 3인칭 카메라.
//
// TestCameraScript 는 WASD 로 카메라를 직접 움직이는데, 캐릭터도 같은 키를 쓰면
// 둘이 서로 다른 속도로 움직여 화면이 흔들린다. 캐릭터 데모에서는 카메라가
// 따라가는 쪽이 맞다.
//
// 오프셋은 월드 기준 고정이다. 대상의 회전을 따라 돌게 하면 제자리에서 방향만
// 바꿔도 카메라가 크게 휘둘려 오히려 보기 불편하다.
class FollowCamera : public MonoBehaviour
{
public:
	FollowCamera();
	virtual ~FollowCamera();

	virtual void LateUpdate() override;

	void SetTarget(shared_ptr<GameObject> target) { _target = target; }
	void SetOffset(const Vec3& offset) { _offset = offset; }
	void SetLookHeight(float height) { _lookHeight = height; }
	void SetDamping(float damping) { _damping = damping; }

private:
	weak_ptr<GameObject>	_target;

	Vec3	_offset = Vec3(0.f, 120.f, -320.f);	// 대상 기준 카메라 위치
	float	_lookHeight = 90.f;					// 대상의 발밑이 아니라 몸통을 본다
	float	_damping = 6.f;						// 클수록 빠르게 따라붙는다
	bool	_snapped = false;					// 첫 프레임은 보간 없이 바로 붙는다
};
