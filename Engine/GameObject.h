#pragma once
#include "Component.h"
#include "Object.h"

class Transform;
class MeshRenderer;
class Camera;
class Light;
class MonoBehaviour;
class ParticleSystem;
class Terrain;
class BaseCollider;
class Animator;

class GameObject : public Object, public enable_shared_from_this<GameObject>
{
public:
	GameObject();
	virtual ~GameObject();

	void Awake();
	void Start();
	void Update();
	void LateUpdate();
	void FinalUpdate();

	shared_ptr<Component> GetFixedComponent(COMPONENT_TYPE type);

	shared_ptr<Transform> GetTransform();
	shared_ptr<MeshRenderer> GetMeshRenderer();
	shared_ptr<Camera> GetCamera();
	shared_ptr<Light> GetLight();
	shared_ptr<ParticleSystem> GetParticleSystem();
	shared_ptr<class BillboardRenderer> GetBillboardRenderer();
	shared_ptr<Terrain> GetTerrain();
	shared_ptr<BaseCollider> GetCollider();
	shared_ptr<Animator> GetAnimator();

	void AddComponent(shared_ptr<Component> component);

	void SetCheckFrustum(bool checkFrustum) { _checkFrustum = checkFrustum; }
	bool GetCheckFrustum() { return _checkFrustum; }

	// 절두체 검사용 월드 공간 바운딩 구.
	// 메시가 없으면 false 를 돌려주고, 그때 호출부는 컬링을 건너뛴다.
	// 예전에는 트랜스폼의 로컬 스케일만 보고 반지름을 정했다. 로드한 메시가
	// 로컬 공간에서 얼마나 뻗어 있는지와 아무 상관이 없는 값이라,
	// 절두체 컬링을 켜는 순간 화면 가장자리에서 물체가 사라졌다.
	// (그래서 씬의 거의 모든 오브젝트가 SetCheckFrustum(false) 였다)
	bool GetWorldBoundingSphere(Vec3& outCenter, float& outRadius);

	void SetLayerIndex(uint8 layer) { _layerIndex = layer; }
	uint8 GetLayerIndex() { return _layerIndex; }

	void SetStatic(bool flag) { _static = flag; }
	bool IsStatic() { return _static; }

private:
	array<shared_ptr<Component>, FIXED_COMPONENT_COUNT> _components;
	vector<shared_ptr<MonoBehaviour>> _scripts;

	bool _checkFrustum = true;
	uint8 _layerIndex = 0;
	bool _static = true;
};

