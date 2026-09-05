#pragma once
#include "Component.h"
#include "RenderTargetGroup.h"

enum class LIGHT_TYPE : uint8
{
	DIRECTIONAL_LIGHT,
	POINT_LIGHT,
	SPOT_LIGHT,
};

struct LightColor
{
	Vec4	diffuse;
	Vec4	ambient;
	Vec4	specular;
};

struct LightInfo
{
	LightColor	color;
	Vec4		position;
	Vec4		direction;
	int32		lightType;
	float		range;
	float		angle;
	int32		padding;
};

struct LightParams
{
	uint32		lightCount;
	Vec3		padding;
	LightInfo	lights[50];
};

class Light : public Component
{
public:
	Light();
	virtual ~Light();

	virtual void FinalUpdate() override;
	void Render();
	void RenderShadow();

public:
	LIGHT_TYPE GetLightType() { return static_cast<LIGHT_TYPE>(_lightInfo.lightType); }

	const LightInfo& GetLightInfo() { return _lightInfo; }

	void SetLightDirection(Vec3 direction);

	void SetDiffuse(const Vec3& diffuse) { _lightInfo.color.diffuse = diffuse; }
	void SetAmbient(const Vec3& ambient) { _lightInfo.color.ambient = ambient; }
	void SetSpecular(const Vec3& specular) { _lightInfo.color.specular = specular; }

	void SetLightType(LIGHT_TYPE type);
	void SetLightRange(float range) { _lightInfo.range = range; }
	void SetLightAngle(float angle) { _lightInfo.angle = angle; }

	void SetLightIndex(int8 index) { _lightIndex = index; }

	// 그림자를 드리울 최대 거리. 카메라 far 를 그대로 쓰면 셰도우 맵 해상도가
	// 아무 데도 못 미친다. 여기까지만 나눠 담는다.
	void SetShadowDistance(float value) { _shadowDistance = value; }
	void SetShadowBias(float value) { _shadowBias = value; }
	// 구간 경계에서 두 캐스케이드를 겹쳐 섞는 폭. 0 이면 끈다.
	void SetCascadeBlend(float value) { _cascadeBlend = value; }

private:
	// 카메라 절두체를 거리로 잘라 구간마다 셰도우 맵을 따로 맞춘다.
	// 가까운 구간은 좁은 영역을 같은 해상도로 덮으니 그만큼 촘촘해진다.
	void UpdateCascades();

private:
	LightInfo _lightInfo = {};

	int8 _lightIndex = -1;
	shared_ptr<class Mesh> _volumeMesh;
	shared_ptr<class Material> _lightMaterial;

	shared_ptr<GameObject> _shadowCamera;

	// 캐스케이드
	array<Matrix, SHADOW_CASCADE_COUNT>	_cascadeView = {};
	array<Matrix, SHADOW_CASCADE_COUNT>	_cascadeProj = {};
	array<Matrix, SHADOW_CASCADE_COUNT>	_cascadeVP = {};
	array<float, SHADOW_CASCADE_COUNT>	_cascadeSplit = {};		// 뷰 공간 깊이 경계
	array<float, SHADOW_CASCADE_COUNT>	_cascadeTexelWorld = {};	// 텍셀 하나의 월드 크기

	float _shadowDistance = 3000.f;
	float _cascadeLambda = 0.5f;	// 0 이면 균등 분할, 1 이면 로그 분할
	float _shadowBias = 0.0015f;
	float _cascadeBlend = 0.12f;

	// F1 로 켠다. 어느 픽셀이 몇 번 캐스케이드를 쓰는지 색으로 보여준다.
	bool _cascadeDebug = false;
};

