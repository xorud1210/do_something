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
//
// 조작
//   WASD        이동
//   Shift       걷기 (기본은 달리기)
//   Q (누르는 중) 시선 고정. 바라보는 방향을 유지한 채 옆/뒤로 움직인다.
//                 이때 이동 방향과 바라보는 방향이 갈라지고, 그 차이가
//                 곧 2D 블렌드 스페이스의 좌표가 된다.
//   좌클릭       공격 (상체 레이어)
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

	// 2D 블렌드 스페이스
	void BuildBlendSpace(shared_ptr<Animator> animator);
	void SetBlendParamAll(const Vec2& param);

private:
	vector<weak_ptr<GameObject>>	_parts;

	float	_moveSpeed = 250.f;
	float	_turnSpeed = 10.f;		// 이동 방향으로 돌아가는 속도 (rad/s 계수)
	float	_yaw = 0.f;				// 현재 바라보는 방향

	// 블렌드 좌표는 입력이 바뀌는 순간 튀면 안 된다.
	// 목표값으로 서서히 따라가게 해서 클립 전환이 부드럽게 이어지게 한다.
	Vec2	_blendParam = Vec2(0.f, 0.f);
	float	_blendFollowSpeed = 6.f;

	bool	_attacking = false;
	bool	_hasBlendSpace = false;
	wstring	_currentClip;
};
