#pragma once
#include "InstancingBuffer.h"

class GameObject;

// 인스턴싱 버퍼는 '드로우 하나당 하나' 다.
//
// 예전에는 인스턴스 ID 로 버퍼를 찾아 썼는데, 비우는 것은 프레임당 한 번이고
// 채우는 것은 카메라마다였다. 두 카메라가 같은 묶음을 보면 두 번째가 같은
// 버퍼에 덧붙이고 다시 업로드한다. 인스턴스 개수는 기록 시점에 굳지만
// 버퍼 내용은 실행 시점에 읽히므로, 먼저 기록한 드로우가 나중 카메라의
// 트랜스폼을 그리게 된다.
//
// 프레임 안에서 꺼내 쓰는 풀로 바꿔서 드로우마다 다른 버퍼를 쓰게 했다.
class InstancingManager
{
	DECLARE_SINGLE(InstancingManager);

public:
	void Render(vector<shared_ptr<GameObject>>& gameObjects);

	// 프레임 머리에서 풀을 되감는다.
	void ClearBuffer();
	void Clear() { _pool.clear(); _used = 0; }

private:
	// 이번 프레임에서 아직 안 쓴 버퍼를 하나 꺼낸다. 모자라면 만든다.
	shared_ptr<InstancingBuffer> Take();

private:
	vector<shared_ptr<InstancingBuffer>>	_pool;
	uint32									_used = 0;
};

