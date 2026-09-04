#pragma once

class GameObject;
// 렌더타겟 그룹 종류. 정의는 RenderTargetGroup.h 에 있다.
enum class RENDER_TARGET_GROUP_TYPE : uint8;


class Scene
{
public:
	void Awake();
	void Start();
	void Update();
	void LateUpdate();
	void FinalUpdate();

	shared_ptr<class Camera> GetMainCamera();

	void Render();

	void ClearRTV();

	void RenderShadow();
	void RenderDeferred();
	void RenderLights();
	void RenderFinal();

	void RenderForward();

	// 블룸 추출 -> 블러 -> 톤매핑. 여기서 처음으로 백버퍼에 그린다.
	void RenderPostProcess();

	// 톤매핑이 끝난 뒤에 백버퍼에 얹는다. UI 는 톤매핑을 타면 안 된다.
	void RenderUI();

	void SetExposure(float value) { _exposure = value; }
	void SetBloomThreshold(float value) { _bloomThreshold = value; }
	void SetBloomIntensity(float value) { _bloomIntensity = value; }

private:
	void PushLightData();

	// 블러 한 번. src 를 읽어 dstGroup 의 dstIndex 에 그린다.
	// stepUV 는 탭 사이의 UV 이동량이라 가로 패스면 (1/w, 0), 세로면 (0, 1/h).
	void BlurPass(shared_ptr<class Texture> src, RENDER_TARGET_GROUP_TYPE dstGroup,
		uint32 dstIndex, Vec2 stepUV);

public:
	void AddGameObject(shared_ptr<GameObject> gameObject);
	void RemoveGameObject(shared_ptr<GameObject> gameObject);

	const vector<shared_ptr<GameObject>>& GetGameObjects() { return _gameObjects; }

private:
	vector<shared_ptr<GameObject>>		_gameObjects;
	vector<shared_ptr<class Camera>>	_cameras;
	vector<shared_ptr<class Light>>		_lights;

	// 톤매핑 / 블룸 조절값
	float	_exposure = 0.9f;
	float	_bloomThreshold = 1.15f;	// 이 밝기를 넘는 만큼만 번진다
	float	_bloomKnee = 0.5f;		// 임계값 근처를 부드럽게 넘기는 폭
	float	_bloomIntensity = 0.45f;
};

