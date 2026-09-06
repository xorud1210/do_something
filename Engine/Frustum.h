#pragma once

enum PLANE_TYPE : uint8
{
	PLANE_FRONT,
	PLANE_BACK,
	PLANE_UP,
	PLANE_DOWN,
	PLANE_LEFT,
	PLANE_RIGHT,

	PLANE_END
};

class Frustum
{
public:
	// 카메라가 자기 행렬을 넘긴다.
	//
	// 예전에는 static 인 Camera::S_MatView / S_MatProjection 을 읽었다.
	// 그 값은 "마지막으로 그린 카메라" 의 것이라, UI 카메라가 있는 씬에서는
	// 직교 투영 볼륨으로 절두체를 만들고 있었다. 컬링이 실제로 무언가를
	// 지우기 시작하자 바로 드러났다 - 캐릭터가 통째로 사라졌다.
	void FinalUpdate(const Matrix& matView, const Matrix& matProjection);
	bool ContainsSphere(const Vec3& pos, float radius);

private:
	array<Vec4, PLANE_END> _planes;
};

