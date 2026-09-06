#pragma once
#include "Component.h"

class Transform : public Component
{
public:
	Transform();
	virtual ~Transform();

	virtual void FinalUpdate() override;
	void PushData();

public:
	// Parent 기준
	const Vec3& GetLocalPosition() { return _localPosition; }
	const Vec3& GetLocalRotation() { return _localRotation; }
	const Vec3& GetLocalScale() { return _localScale; }

	// 부모까지 곱해진 스케일 중 가장 큰 값.
	// 구는 비균등 스케일을 담을 수 없으므로 가장 큰 축을 쓴다 - 크게 잡는 쪽이
	// 안전하다(안 보이는 걸 그릴 뿐, 보이는 걸 지우지 않는다).
	float GetMaxWorldScale() const
	{
		const float x = Vec3(_matWorld._11, _matWorld._12, _matWorld._13).Length();
		const float y = Vec3(_matWorld._21, _matWorld._22, _matWorld._23).Length();
		const float z = Vec3(_matWorld._31, _matWorld._32, _matWorld._33).Length();
		return (x > y) ? ((x > z) ? x : z) : ((y > z) ? y : z);
	}

	const Matrix& GetLocalToWorldMatrix() { return _matWorld; }
	Vec3 GetWorldPosition() { return _matWorld.Translation(); }

	Vec3 GetRight() { return _matWorld.Right(); }
	Vec3 GetUp() { return _matWorld.Up(); }
	Vec3 GetLook() { return _matWorld.Backward(); }

	void SetLocalPosition(const Vec3& position) { _localPosition = position; }
	void SetLocalRotation(const Vec3& rotation) { _localRotation = rotation; }
	void SetLocalScale(const Vec3& scale) { _localScale = scale; }

	void LookAt(const Vec3& dir);

	static bool CloseEnough(const float& a, const float& b, const float& epsilon = std::numeric_limits<float>::epsilon());
	static Vec3 DecomposeRotationMatrix(const Matrix& rotation);

public:
	void SetParent(shared_ptr<Transform> parent) { _parent = parent; }
	weak_ptr<Transform> GetParent() { return _parent; }

private:
	// Parent 기준
	Vec3 _localPosition = {};
	Vec3 _localRotation = {};
	Vec3 _localScale = { 1.f, 1.f, 1.f };

	Matrix _matLocal= {};
	Matrix _matWorld = {};

	weak_ptr<Transform> _parent;
};

