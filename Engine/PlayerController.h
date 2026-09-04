#pragma once
#include "MonoBehaviour.h"

class GameObject;
class Animator;

// .bin 캐릭터를 움직이고 애니메이션을 갈아끼우는 최소 컨트롤러.
//
// Player.bin 은 파츠(머리 / 몸통 / 장갑 ...)가 메시마다 따로 떨어져 나오므로
// GameObject 도 9 개가 만들어진다. 루트 하나가 이동을 담당하고, 파츠는
// Transform 부모로 붙여 따라오게 한다. 애니메이션은 파츠마다 Animator 가
// 있으므로 이 스크립트가 목록을 들고 한꺼번에 지시한다.
class PlayerController : public MonoBehaviour
{
public:
	PlayerController();
	virtual ~PlayerController();

	virtual void LateUpdate() override;

	// 이 캐릭터를 구성하는 파츠(= Animator 를 가진 GameObject)를 등록한다.
	void AddPart(shared_ptr<GameObject> part);

	void SetMoveSpeed(float speed) { _moveSpeed = speed; }
	void SetFacing(float yawRadian) { _yaw = yawRadian; }

private:
	// 등록된 모든 파츠에 같은 클립을 지시한다.
	void PlayAll(const wstring& clipName, float fade, bool loop);

	// 상체 레이어. 하체는 그대로 두고 상체만 다른 클립으로 덮어쓴다.
	void PlayUpperAll(const wstring& clipName, float fade, bool loop);
	void StopUpperAll(float fade);
	bool UpperFinished() const;

private:
	vector<weak_ptr<GameObject>>	_parts;

	float	_moveSpeed = 250.f;
	float	_turnSpeed = 10.f;		// 이동 방향으로 돌아가는 속도 (rad/s 계수)
	float	_yaw = 0.f;				// 현재 바라보는 방향

	bool	_attacking = false;
	wstring	_currentClip;
};
