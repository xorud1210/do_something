#include "pch.h"
#include "Scene.h"
#include "GameObject.h"
#include "Camera.h"
#include "Engine.h"
#include "ConstantBuffer.h"
#include "Light.h"
#include "Engine.h"
#include "Resources.h"
#include "Material.h"
#include "Mesh.h"
#include "Texture.h"

void Scene::Awake()
{
	for (const shared_ptr<GameObject>& gameObject : _gameObjects)
	{
		gameObject->Awake();
	}
}

void Scene::Start()
{
	for (const shared_ptr<GameObject>& gameObject : _gameObjects)
	{
		gameObject->Start();
	}
}

void Scene::Update()
{
	for (const shared_ptr<GameObject>& gameObject : _gameObjects)
	{
		gameObject->Update();
	}
}

void Scene::LateUpdate()
{
	for (const shared_ptr<GameObject>& gameObject : _gameObjects)
	{
		gameObject->LateUpdate();
	}
}

void Scene::FinalUpdate()
{
	for (const shared_ptr<GameObject>& gameObject : _gameObjects)
	{
		gameObject->FinalUpdate();
	}
}

shared_ptr<Camera> Scene::GetMainCamera()
{
	if (_cameras.empty())
		return nullptr;

	return _cameras[0];
}

void Scene::Render()
{
	PushLightData();

	ClearRTV();

	RenderShadow();

	RenderDeferred();

	RenderLights();

	// 여기부터 RenderForward 까지는 전부 HDR 타겟에 그린다.
	RenderFinal();

	RenderForward();

	// HDR 을 읽어서 백버퍼로 내보낸다.
	RenderPostProcess();

	// UI 는 톤매핑 밖. 백버퍼에 직접.
	RenderUI();
}

void Scene::ClearRTV()
{
	// SwapChain Group 초기화
	int8 backIndex = GEngine->GetSwapChain()->GetBackBufferIndex();
	GEngine->GetRTGroup(RENDER_TARGET_GROUP_TYPE::SWAP_CHAIN)->ClearRenderTargetView(backIndex);
	// Shadow Group 초기화
	GEngine->GetRTGroup(RENDER_TARGET_GROUP_TYPE::SHADOW)->ClearRenderTargetView();
	// Deferred Group 초기화
	GEngine->GetRTGroup(RENDER_TARGET_GROUP_TYPE::G_BUFFER)->ClearRenderTargetView();
	// Lighting Group 초기화
	GEngine->GetRTGroup(RENDER_TARGET_GROUP_TYPE::LIGHTING)->ClearRenderTargetView();
	// HDR Group 초기화
	GEngine->GetRTGroup(RENDER_TARGET_GROUP_TYPE::HDR)->ClearRenderTargetView();
	// Bloom Group 초기화
	GEngine->GetRTGroup(RENDER_TARGET_GROUP_TYPE::BLOOM_HALF)->ClearRenderTargetView();
	GEngine->GetRTGroup(RENDER_TARGET_GROUP_TYPE::BLOOM_QUARTER)->ClearRenderTargetView();
}

void Scene::RenderShadow()
{
	GEngine->GetRTGroup(RENDER_TARGET_GROUP_TYPE::SHADOW)->OMSetRenderTargets();

	for (auto& light : _lights)
	{
		if (light->GetLightType() != LIGHT_TYPE::DIRECTIONAL_LIGHT)
			continue;

		light->RenderShadow();
	}

	GEngine->GetRTGroup(RENDER_TARGET_GROUP_TYPE::SHADOW)->WaitTargetToResource();
}

void Scene::RenderDeferred()
{
	// Deferred OMSet
	GEngine->GetRTGroup(RENDER_TARGET_GROUP_TYPE::G_BUFFER)->OMSetRenderTargets();

	shared_ptr<Camera> mainCamera = _cameras[0];
	mainCamera->SortGameObject();
	mainCamera->Render_Deferred();

	GEngine->GetRTGroup(RENDER_TARGET_GROUP_TYPE::G_BUFFER)->WaitTargetToResource();
}

void Scene::RenderLights()
{
	shared_ptr<Camera> mainCamera = _cameras[0];
	Camera::S_MatView = mainCamera->GetViewMatrix();
	Camera::S_MatProjection = mainCamera->GetProjectionMatrix();

	GEngine->GetRTGroup(RENDER_TARGET_GROUP_TYPE::LIGHTING)->OMSetRenderTargets();

	// 광원을 그린다.
	for (auto& light : _lights)
	{
		light->Render();
	}

	GEngine->GetRTGroup(RENDER_TARGET_GROUP_TYPE::LIGHTING)->WaitTargetToResource();
}

void Scene::RenderFinal()
{
	// 알베도 * 조명 + 스페큘러를 합쳐 HDR 타겟에 쓴다.
	// 예전에는 여기서 바로 백버퍼로 갔고, 그래서 1 을 넘는 값이 그 자리에서 잘렸다.
	GEngine->GetRTGroup(RENDER_TARGET_GROUP_TYPE::HDR)->OMSetRenderTargets(1, 0);

	GET_SINGLE(Resources)->Get<Material>(L"Final")->PushGraphicsData();
	GET_SINGLE(Resources)->Get<Mesh>(L"Rectangle")->Render();
}

void Scene::RenderForward()
{
	// 스카이박스, 파티클, 포워드 오브젝트도 같은 HDR 타겟에 얹는다.
	// 여기 있는 것들도 블룸과 톤매핑을 같이 받아야 한 화면처럼 보인다.
	// RenderFinal 이 이미 이 그룹을 걸어놨으므로 다시 걸지 않는다(지우면 안 된다).
	shared_ptr<Camera> mainCamera = _cameras[0];
	mainCamera->Render_Forward();

	GEngine->GetRTGroup(RENDER_TARGET_GROUP_TYPE::HDR)->WaitTargetToResource();
}

void Scene::BlurPass(shared_ptr<Texture> src, RENDER_TARGET_GROUP_TYPE dstGroup,
	uint32 dstIndex, Vec2 stepUV)
{
	shared_ptr<RenderTargetGroup> group = GEngine->GetRTGroup(dstGroup);
	group->OMSetRenderTargets(1, dstIndex);

	shared_ptr<Material> material = GET_SINGLE(Resources)->Get<Material>(L"Blur");
	material->SetTexture(0, src);
	material->SetVec2(0, stepUV);
	material->PushGraphicsData();

	GET_SINGLE(Resources)->Get<Mesh>(L"Rectangle")->Render();

	group->WaitTargetToResource(dstIndex);
}

void Scene::RenderPostProcess()
{
	Resources* resources = GET_SINGLE(Resources);
	shared_ptr<Mesh> rect = resources->Get<Mesh>(L"Rectangle");

	shared_ptr<RenderTargetGroup> half = GEngine->GetRTGroup(RENDER_TARGET_GROUP_TYPE::BLOOM_HALF);
	shared_ptr<RenderTargetGroup> quarter = GEngine->GetRTGroup(RENDER_TARGET_GROUP_TYPE::BLOOM_QUARTER);

	const float halfWidth = half->GetRTTexture(0)->GetWidth();
	const float halfHeight = half->GetRTTexture(0)->GetHeight();
	const float quarterWidth = quarter->GetRTTexture(0)->GetWidth();
	const float quarterHeight = quarter->GetRTTexture(0)->GetHeight();

	// 1. 밝은 부분만 뽑는다. 이때 절반 해상도로 내려간다.
	{
		half->OMSetRenderTargets(1, 0);

		shared_ptr<Material> material = resources->Get<Material>(L"BrightPass");
		material->SetFloat(0, _bloomThreshold);
		material->SetFloat(1, _bloomKnee);
		material->PushGraphicsData();

		rect->Render();

		half->WaitTargetToResource(0);
	}

	// 2. 1/2 해상도에서 가로 -> 세로.
	//    핑퐁이라 0번을 읽어 1번에 쓰고, 다시 1번을 읽어 0번에 쓴다.
	BlurPass(half->GetRTTexture(0), RENDER_TARGET_GROUP_TYPE::BLOOM_HALF, 1, Vec2(1.f / halfWidth, 0.f));

	half->WaitResourceToTarget(0);
	BlurPass(half->GetRTTexture(1), RENDER_TARGET_GROUP_TYPE::BLOOM_HALF, 0, Vec2(0.f, 1.f / halfHeight));

	// 3. 1/4 해상도로 한 단계 더. 같은 탭 수로 두 배 넓게 번진다.
	//    가로 패스가 1/2 짜리를 읽으면서 다운샘플까지 겸한다.
	BlurPass(half->GetRTTexture(0), RENDER_TARGET_GROUP_TYPE::BLOOM_QUARTER, 1, Vec2(1.f / quarterWidth, 0.f));
	BlurPass(quarter->GetRTTexture(1), RENDER_TARGET_GROUP_TYPE::BLOOM_QUARTER, 0, Vec2(0.f, 1.f / quarterHeight));

	// 4. HDR + 블룸 두 장을 합쳐 0~1 로 눌러 담고 백버퍼로.
	{
		int8 backIndex = GEngine->GetSwapChain()->GetBackBufferIndex();
		GEngine->GetRTGroup(RENDER_TARGET_GROUP_TYPE::SWAP_CHAIN)->OMSetRenderTargets(1, backIndex);

		shared_ptr<Material> material = resources->Get<Material>(L"Tonemap");
		material->SetFloat(0, _exposure);
		material->SetFloat(1, _bloomIntensity);
		material->PushGraphicsData();

		rect->Render();
	}
}

void Scene::RenderUI()
{
	// 백버퍼는 톤매핑 패스가 이미 걸어놨다.
	shared_ptr<Camera> mainCamera = _cameras[0];

	for (auto& camera : _cameras)
	{
		if (camera == mainCamera)
			continue;

		camera->SortGameObject();
		camera->Render_Forward();
	}
}

void Scene::PushLightData()
{
	LightParams lightParams = {};

	// lightParams 는 지역 변수다. lights[] 를 넘겨 쓰면 스택이 깨진다.
	// 상수 버퍼 크기가 곧 상한이므로 배열 크기에서 직접 가져온다.
	constexpr uint32 maxLightCount = static_cast<uint32>(_countof(lightParams.lights));

	for (auto& light : _lights)
	{
		if (lightParams.lightCount >= maxLightCount)
		{
			// 못 담은 광원은 인덱스를 지워서 자기 볼륨도 그리지 않게 한다.
			// 인덱스를 남겨두면 남의 자리를 읽는다.
			assert(false);
			light->SetLightIndex(-1);
			continue;
		}

		const LightInfo& lightInfo = light->GetLightInfo();

		light->SetLightIndex(static_cast<int8>(lightParams.lightCount));

		lightParams.lights[lightParams.lightCount] = lightInfo;
		lightParams.lightCount++;
	}

	CONST_BUFFER(CONSTANT_BUFFER_TYPE::GLOBAL)->SetGraphicsGlobalData(&lightParams, sizeof(lightParams));
}

void Scene::AddGameObject(shared_ptr<GameObject> gameObject)
{
	if (gameObject->GetCamera() != nullptr)
	{
		_cameras.push_back(gameObject->GetCamera());
	}
	else if (gameObject->GetLight() != nullptr)
	{
		_lights.push_back(gameObject->GetLight());
	}

	_gameObjects.push_back(gameObject);
}

void Scene::RemoveGameObject(shared_ptr<GameObject> gameObject)
{
	if (gameObject->GetCamera())
	{
		auto findIt = std::find(_cameras.begin(), _cameras.end(), gameObject->GetCamera());
		if (findIt != _cameras.end())
			_cameras.erase(findIt);
	}
	else if (gameObject->GetLight())
	{
		auto findIt = std::find(_lights.begin(), _lights.end(), gameObject->GetLight());
		if (findIt != _lights.end())
			_lights.erase(findIt);
	}

	auto findIt = std::find(_gameObjects.begin(), _gameObjects.end(), gameObject);
	if (findIt != _gameObjects.end())
		_gameObjects.erase(findIt);
}