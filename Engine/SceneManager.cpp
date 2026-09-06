#include "pch.h"
#include "SceneManager.h"
#include "Scene.h"

#include "Engine.h"
#include "Material.h"
#include "GameObject.h"
#include "MeshRenderer.h"
#include "Transform.h"
#include "Camera.h"
#include "Light.h"

#include "TestCameraScript.h"
#include "Resources.h"
#include "ParticleSystem.h"
#include "BillboardRenderer.h"
#include "Terrain.h"
#include "SphereCollider.h"
#include "MeshData.h"
#include "Animator.h"
#include "PlayerController.h"
#include "FollowCamera.h"

#include <random>


void SceneManager::Update()
{
	if (_activeScene == nullptr)
		return;

	_activeScene->Update();
	_activeScene->LateUpdate();
	_activeScene->FinalUpdate();
}

// TEMP
void SceneManager::Render()
{
	if (_activeScene)
		_activeScene->Render();
}

void SceneManager::LoadScene(wstring sceneName)
{
	// TODO : 占쏙옙占쏙옙 Scene 占쏙옙占쏙옙
	// TODO : 占쏙옙占싹울옙占쏙옙 Scene 占쏙옙占쏙옙 占싸듸옙

	_activeScene = LoadTestScene();

	_activeScene->Awake();
	_activeScene->Start();
}

void SceneManager::SetLayerName(uint8 index, const wstring& name)
{
	// 占쏙옙占쏙옙 占쏙옙占쏙옙占쏙옙 占쏙옙占쏙옙
	const wstring& prevName = _layerNames[index];
	_layerIndex.erase(prevName);

	_layerNames[index] = name;
	_layerIndex[name] = index;
}

uint8 SceneManager::LayerNameToIndex(const wstring& name)
{
	auto findIt = _layerIndex.find(name);
	if (findIt == _layerIndex.end())
		return 0;

	return findIt->second;
}

shared_ptr<GameObject> SceneManager::Pick(int32 screenX, int32 screenY)
{
	shared_ptr<Camera> camera = GetActiveScene()->GetMainCamera();

	float width = static_cast<float>(GEngine->GetWindow().width);
	float height = static_cast<float>(GEngine->GetWindow().height);

	Matrix projectionMatrix = camera->GetProjectionMatrix();

	// ViewSpace占쏙옙占쏙옙 Picking 占쏙옙占쏙옙
	float viewX = (+2.0f * screenX / width - 1.0f) / projectionMatrix(0, 0);
	float viewY = (-2.0f * screenY / height + 1.0f) / projectionMatrix(1, 1);

	Matrix viewMatrix = camera->GetViewMatrix();
	Matrix viewMatrixInv = viewMatrix.Invert();

	auto& gameObjects = GET_SINGLE(SceneManager)->GetActiveScene()->GetGameObjects();

	float minDistance = FLT_MAX;
	shared_ptr<GameObject> picked;

	for (auto& gameObject : gameObjects)
	{
		if (gameObject->GetCollider() == nullptr)
			continue;

		// ViewSpace占쏙옙占쏙옙占쏙옙 Ray 占쏙옙占쏙옙
		Vec4 rayOrigin = Vec4(0.0f, 0.0f, 0.0f, 1.0f);
		Vec4 rayDir = Vec4(viewX, viewY, 1.0f, 0.0f);

		// WorldSpace占쏙옙占쏙옙占쏙옙 Ray 占쏙옙占쏙옙
		rayOrigin = XMVector3TransformCoord(rayOrigin, viewMatrixInv);
		rayDir = XMVector3TransformNormal(rayDir, viewMatrixInv);
		rayDir.Normalize();

		// WorldSpace占쏙옙占쏙옙 占쏙옙占쏙옙
		float distance = 0.f;
		if (gameObject->GetCollider()->Intersects(rayOrigin, rayDir, OUT distance) == false)
			continue;

		if (distance < minDistance)
		{
			minDistance = distance;
			picked = gameObject;
		}
	}

	return picked;
}
shared_ptr<Scene> SceneManager::LoadTestScene()
{
#pragma region LayerMask
	SetLayerName(0, L"Default");
	SetLayerName(1, L"UI");
#pragma endregion


	shared_ptr<Scene> scene = make_shared<Scene>();

	// Kept outside the block so the follow camera can be wired to the player below.
	shared_ptr<GameObject> mainCamera;
	
#pragma region Camera
	{
		shared_ptr<GameObject> camera = make_shared<GameObject>();
		camera->SetName(L"Main_Camera");
		camera->AddComponent(make_shared<Transform>());
		camera->AddComponent(make_shared<Camera>()); // Near=1, Far=1000, FOV=45占쏙옙
		// TestCameraScript moves the camera with WASD, which fights the player for
		// the same keys. A follow camera is attached after the player is created.
		mainCamera = camera;
		camera->GetCamera()->SetMain(true);
		camera->GetCamera()->SetFar(10000.f);
		camera->GetTransform()->SetLocalPosition(Vec3(0.f, 0.f, 0.f));
		uint8 layerIndex = GET_SINGLE(SceneManager)->LayerNameToIndex(L"UI");
		camera->GetCamera()->SetCullingMaskLayerOnOff(layerIndex, true); // UI占쏙옙 占쏙옙 占쏙옙占쏙옙
		scene->AddGameObject(camera);
	}	
#pragma endregion

#pragma region UI_Camera
	{
		shared_ptr<GameObject> camera = make_shared<GameObject>();
		camera->SetName(L"Orthographic_Camera");
		camera->AddComponent(make_shared<Transform>());
		camera->AddComponent(make_shared<Camera>()); // Near=1, Far=1000, 800*600
		camera->GetTransform()->SetLocalPosition(Vec3(0.f, 0.f, 0.f));
		camera->GetCamera()->SetProjectionType(PROJECTION_TYPE::ORTHOGRAPHIC);
		uint8 layerIndex = GET_SINGLE(SceneManager)->LayerNameToIndex(L"UI");
		camera->GetCamera()->SetCullingMaskAll(); // 占쏙옙 占쏙옙占쏙옙
		camera->GetCamera()->SetCullingMaskLayerOnOff(layerIndex, false); // UI占쏙옙 占쏙옙占쏙옙
		scene->AddGameObject(camera);
	}
#pragma endregion

#pragma region SkyBox
	{
		shared_ptr<GameObject> skybox = make_shared<GameObject>();
		skybox->AddComponent(make_shared<Transform>());
		skybox->SetCheckFrustum(false);
		shared_ptr<MeshRenderer> meshRenderer = make_shared<MeshRenderer>();
		{
			shared_ptr<Mesh> sphereMesh = GET_SINGLE(Resources)->LoadSphereMesh();
			meshRenderer->SetMesh(sphereMesh);
		}
		{
			shared_ptr<Shader> shader = GET_SINGLE(Resources)->Get<Shader>(L"Skybox");
			shared_ptr<Texture> texture = GET_SINGLE(Resources)->LoadTexture(L"..\\Resources\\Texture\\Sky01.jpg", true);
			shared_ptr<Material> material = make_shared<Material>();
			material->SetShader(shader);
			material->SetTexture(0, texture);
			meshRenderer->SetMaterial(material);
		}
		skybox->AddComponent(meshRenderer);
		scene->AddGameObject(skybox);
	}
#pragma endregion

#pragma region Object
	/*{
		shared_ptr<GameObject> obj = make_shared<GameObject>();
		obj->SetName(L"OBJ");
		obj->AddComponent(make_shared<Transform>());
		obj->AddComponent(make_shared<SphereCollider>());
		obj->GetTransform()->SetLocalScale(Vec3(100.f, 100.f, 100.f));
		obj->GetTransform()->SetLocalPosition(Vec3(0, 0.f, 500.f));
		obj->SetStatic(false);
		shared_ptr<MeshRenderer> meshRenderer = make_shared<MeshRenderer>();
		{
			shared_ptr<Mesh> sphereMesh = GET_SINGLE(Resources)->LoadSphereMesh();
			meshRenderer->SetMesh(sphereMesh);
		}
		{
			shared_ptr<Material> material = GET_SINGLE(Resources)->Get<Material>(L"GameObject");
			meshRenderer->SetMaterial(material->Clone());
		}
		dynamic_pointer_cast<SphereCollider>(obj->GetCollider())->SetRadius(0.5f);
		dynamic_pointer_cast<SphereCollider>(obj->GetCollider())->SetCenter(Vec3(0.f, 0.f, 0.f));
		obj->AddComponent(meshRenderer);
		scene->AddGameObject(obj);
	}*/
#pragma endregion

#pragma region Terrain
	// 바닥. 예전에는 4000 유닛짜리 큐브 한 개였다.
	// 졸업작품의 하이트맵을 가져와 기복이 있는 지형으로 바꿨다.
	//
	// 이 아래의 초목·화톳불·나무·바위는 전부 이 지형 표면 높이에 맞춰 놓는다.
	// 그래서 terrain 을 먼저 만들고 포인터를 들고 있는다.
	shared_ptr<Terrain> terrain;
	{
		shared_ptr<GameObject> obj = make_shared<GameObject>();
		obj->SetName(L"Terrain");
		obj->AddComponent(make_shared<Transform>());
		obj->AddComponent(make_shared<MeshRenderer>());
		obj->AddComponent(make_shared<Terrain>());
		obj->SetStatic(true);

		// 지형은 한 오브젝트가 6000 유닛을 덮는다. 트랜스폼 하나로 잘라낼 수
		// 없으므로 절두체 컬링에서 뺀다 (초목 묶음과 같은 이유).
		obj->SetCheckFrustum(false);

		TerrainDesc desc;
		desc.worldSize = 6000.f;
		desc.heightScale = 320.f;
		desc.center = Vec3(90.f, -95.f, 340.f);
		desc.detailTiling = 26.f;

		terrain = obj->GetTerrain();
		terrain->Init(desc);

		scene->AddGameObject(obj);
	}
#pragma endregion

#pragma region Foliage
	// 인스턴싱 초목.
	// 점 하나를 GS 에서 사각형으로 펼치므로 정점 버퍼에는 점 하나뿐이고,
	// 위치·크기·바람 위상은 StructuredBuffer 에서 인스턴스 ID 로 당겨온다.
	// 수천 장이 드로우콜 하나로 나간다.
	{
		shared_ptr<GameObject> obj = make_shared<GameObject>();
		obj->SetName(L"Grass");
		obj->AddComponent(make_shared<Transform>());
		obj->SetCheckFrustum(false);

		BillboardDesc desc;
		desc.count = 30000;
		desc.center = Vec3(90.f, 0.f, 340.f);
		desc.area = Vec2(2600.f, 2600.f);
		// 지면이 평평하지 않으므로 밑동 높이를 지형에 묻는다.
		desc.heightAt = [terrain](float x, float z) { return terrain->GetHeight(x, z); };
		desc.groundY = -85.f;			// 바닥 윗면
		desc.minScale = 20.f;
		desc.maxScale = 42.f;
		desc.heightRatio = 1.5f;
		desc.holeCenter = Vec3(250.f, 0.f, 460.f);	// 화톳불 자리는 비워둔다
		desc.holeRadius = 130.f;
		desc.windDirection = Vec3(1.f, 0.f, 0.35f);
		desc.windStrength = 6.f;
		desc.windFrequency = 1.5f;
		// 심은 범위(2600)보다 짧게 자르면 풀밭 가장자리에 원형 경계가 눈에 보인다.
		// 페이드 없이 자를 거면 아예 안 자르는 편이 낫다.
		desc.maxDrawDistance = 0.f;
		desc.castShadow = true;

		shared_ptr<BillboardRenderer> billboard = make_shared<BillboardRenderer>();
		billboard->SetDesc(desc);
		obj->AddComponent(billboard);

		scene->AddGameObject(obj);
	}
#pragma endregion

#pragma region Props
	// 나무와 바위. 졸업작품이 쓰던 .bin 모델을 그대로 읽는다.
	//
	// 한 그루가 GameObject 여러 개(줄기 / 가지)로 들어오므로, 심을 때마다
	// Instantiate 를 다시 부르지 않고 프리팹처럼 한 번 읽어 여러 번 찍는다.
	{
		struct PropDesc
		{
			const wchar_t*	path;
			uint32			count;
			float			minScale;
			float			maxScale;
			bool			alignToGround;	// 경사에 맞춰 기울일 것인가

			// 알파 테스트를 걸 재질을 텍스처 이름의 일부로 지정한다. 비어 있으면 안 건다.
			//
			// 나무 한 그루에 재질이 둘이다 - 잎과 기둥. 둘 다에 걸면 기둥이 통째로
			// 사라진다. 기둥 텍스처의 알파 채널에는 불투명도가 아니라 다른 값이
			// 들어 있어서(Tree_Bark 는 최대 58/255, Birch_Bark 는 48/255)
			// 어떤 임계값을 잡아도 전부 잘려 나간다.
			// 임계값을 아주 작게 낮추는 대신, 기둥에는 아예 걸지 않는다.
			const wchar_t*	alphaTestMatch;
		};

		const PropDesc props[] =
		{
			// 모델 실측 높이: 소나무 27.8 / 자작나무 22.0 / 바위 0.6~1.9 유닛.
			// 플레이어가 1.85 x 100 = 185 유닛이라 나무는 그 2~3배로 잡았다.
			{ L"..\\Resources\\Model\\FAE_Pine_A_LOD0.bin", 70, 16.0f, 28.0f, false, L"Branch" },
			{ L"..\\Resources\\Model\\FAE_Birch_A_LOD0.bin", 55, 17.0f, 30.0f, false, L"Branch" },
			{ L"..\\Resources\\Model\\RockCluster_B_LOD0.bin", 26, 25.0f, 70.0f, true , nullptr },
			{ L"..\\Resources\\Model\\RockCluster_C_LOD0.bin", 20, 25.0f, 60.0f, true , nullptr },
			{ L"..\\Resources\\Model\\RockCluster_D.bin", 16, 20.0f, 50.0f, true , nullptr },
		};

		std::mt19937 rng(20260906);
		std::uniform_real_distribution<float> unit(0.f, 1.f);

		// 화톳불과 플레이어 자리는 비워 둔다.
		const Vec2 clearings[] = { Vec2(250.f, 460.f), Vec2(90.f, 340.f) };
		const float clearRadius = 200.f;

		for (const PropDesc& prop : props)
		{
			shared_ptr<MeshData> meshData = GET_SINGLE(Resources)->LoadBin(prop.path);
			if (meshData == nullptr)
				continue;

			// 잎은 알파로 잘라내야 한다. .bin 은 그 사실을 담고 있지 않으므로
			// 심는 쪽에서 정해 준다. 머티리얼은 인스턴스끼리 공유하므로 한 번만 하면 된다.
			if (prop.alphaTestMatch != nullptr)
			{
				vector<shared_ptr<GameObject>> probe = meshData->Instantiate();
				shared_ptr<Shader> alphaShader =
					GET_SINGLE(Resources)->Get<Shader>(L"DeferredAlphaTest");
				const wstring match = prop.alphaTestMatch;

				for (auto& part : probe)
				{
					shared_ptr<MeshRenderer> mr = part->GetMeshRenderer();
					for (uint32 m = 0; m < mr->GetMaterialCount(); m++)
					{
						shared_ptr<Material> mat = mr->GetMaterial(m);
						if (mat == nullptr)
							continue;

						// 디퓨즈 텍스처의 이름으로 잎인지 기둥인지 가른다.
						shared_ptr<Texture> diffuse = mat->GetTexture(0);
						if (diffuse == nullptr)
							continue;
						if (diffuse->GetName().find(match) == wstring::npos)
							continue;

						mat->SetShader(alphaShader);
					}
				}
			}

			uint32 placed = 0;
			for (uint32 attempt = 0; attempt < prop.count * 8 && placed < prop.count; attempt++)
			{
				const float x = 90.f + (unit(rng) * 2.f - 1.f) * 2100.f;
				const float z = 340.f + (unit(rng) * 2.f - 1.f) * 2100.f;

				bool blocked = false;
				for (const Vec2& spot : clearings)
				{
					const float dx = x - spot.x;
					const float dz = z - spot.y;
					if (dx * dx + dz * dz < clearRadius * clearRadius)
						blocked = true;
				}
				if (blocked)
					continue;

				// 너무 가파른 곳에는 나무를 세우지 않는다. 바위는 괜찮다.
				const Vec3 groundNormal = terrain->GetNormal(x, z);
				if (prop.alignToGround == false && groundNormal.y < 0.86f)
					continue;

				const float y = terrain->GetHeight(x, z);
				const float scale = prop.minScale + (prop.maxScale - prop.minScale) * unit(rng);

				vector<shared_ptr<GameObject>> parts = meshData->Instantiate();
				for (auto& part : parts)
				{
					part->SetName(L"Prop");
					part->SetStatic(true);
					part->SetCheckFrustum(true);

					shared_ptr<Transform> transform = part->GetTransform();
					transform->SetLocalScale(Vec3(scale, scale, scale));
					transform->SetLocalPosition(Vec3(x, y, z));

					// 같은 모델을 여러 번 심으므로 Y 회전을 흩뿌려 반복을 감춘다.
					float pitch = 0.f;
					float roll = 0.f;
					if (prop.alignToGround)
					{
						// 바위는 지면 기울기를 따라 눕힌다. 나무는 세워 둔다.
						pitch = ::atan2f(groundNormal.z, groundNormal.y);
						roll = -::atan2f(groundNormal.x, groundNormal.y);
					}
					transform->SetLocalRotation(Vec3(pitch, unit(rng) * XM_2PI, roll));

					scene->AddGameObject(part);
				}

				placed++;
			}


		}
	}
#pragma endregion

#pragma region Particle
	// 화톳불. 지금까지 블룸이 물 수 있는 건 밝게 조명받은 표면뿐이었다.
	// 화면 안에 실제로 빛나는 것을 두면 그때부터 "빛 번짐"이 된다.
	{
		shared_ptr<GameObject> obj = make_shared<GameObject>();
		obj->SetName(L"Campfire");
		obj->AddComponent(make_shared<Transform>());
		obj->GetTransform()->SetLocalPosition(
			Vec3(250.f, terrain->GetHeight(250.f, 460.f), 460.f));
		obj->SetCheckFrustum(false);

		ParticleDesc desc;
		desc.maxParticle = 300;
		desc.createInterval = 0.009f;
		desc.minLifeTime = 0.6f;
		desc.maxLifeTime = 1.3f;
		desc.minSpeed = 60.f;
		desc.maxSpeed = 130.f;
		desc.startScale = 26.f;
		desc.endScale = 4.f;
		desc.startColor = Vec4(1.00f, 0.55f, 0.16f, 1.f);
		desc.endColor = Vec4(0.85f, 0.10f, 0.02f, 0.f);
		desc.emissive = 2.2f;					// HDR 범위로 올려 블룸이 물게 한다
		desc.shape = PARTICLE_EMITTER_SHAPE::CONE;
		desc.radius = 20.f;
		desc.coneAngle = 0.42f;
		desc.gravity = Vec3(0.f, 45.f, 0.f);	// 열기에 떠오른다
		desc.drag = 1.2f;
		desc.maxSpin = 2.0f;
		desc.softFadeDistance = 40.f;
		desc.additive = true;

		shared_ptr<ParticleSystem> particle = make_shared<ParticleSystem>();
		particle->SetDesc(desc);
		obj->AddComponent(particle);

		scene->AddGameObject(obj);
	}

	// 불티. 올라갔다 중력에 떨어진다.
	{
		shared_ptr<GameObject> obj = make_shared<GameObject>();
		obj->SetName(L"Ember");
		obj->AddComponent(make_shared<Transform>());
		obj->GetTransform()->SetLocalPosition(Vec3(250.f, -75.f, 460.f));
		obj->SetCheckFrustum(false);

		ParticleDesc desc;
		desc.maxParticle = 220;
		desc.createInterval = 0.03f;
		desc.minLifeTime = 1.2f;
		desc.maxLifeTime = 2.4f;
		desc.minSpeed = 110.f;
		desc.maxSpeed = 210.f;
		desc.startScale = 6.f;
		desc.endScale = 2.f;
		desc.startColor = Vec4(1.00f, 0.78f, 0.40f, 1.f);
		desc.endColor = Vec4(1.00f, 0.28f, 0.05f, 0.f);
		desc.emissive = 4.5f;
		desc.shape = PARTICLE_EMITTER_SHAPE::CONE;
		desc.radius = 12.f;
		desc.coneAngle = 0.55f;
		desc.gravity = Vec3(0.f, -70.f, 0.f);
		desc.drag = 0.25f;
		desc.maxSpin = 4.f;
		desc.softFadeDistance = 20.f;
		desc.additive = true;
		desc.texturePath = L"..\\Resources\\Texture\\Particle\\spark.png";

		shared_ptr<ParticleSystem> particle = make_shared<ParticleSystem>();
		particle->SetDesc(desc);
		obj->AddComponent(particle);

		scene->AddGameObject(obj);
	}
#pragma endregion

#pragma region UI_Test
	// Debug monitors for the render targets, pinned to the top-left corner.
	// Derived from the window size so they stay put if the resolution changes.
	for (int32 i = 0; i < 7; i++)
	{
		shared_ptr<GameObject> obj = make_shared<GameObject>();
		obj->SetLayerIndex(GET_SINGLE(SceneManager)->LayerNameToIndex(L"UI")); // UI
		obj->AddComponent(make_shared<Transform>());
		obj->GetTransform()->SetLocalScale(Vec3(100.f, 100.f, 100.f));
		obj->GetTransform()->SetLocalPosition(Vec3(
			-(GEngine->GetWindow().width * 0.5f) + 12.f + 50.f + i * 120.f,
			 (GEngine->GetWindow().height * 0.5f) - 12.f - 50.f,
			 500.f));
		shared_ptr<MeshRenderer> meshRenderer = make_shared<MeshRenderer>();
		{
			shared_ptr<Mesh> mesh = GET_SINGLE(Resources)->LoadRectangleMesh();
			meshRenderer->SetMesh(mesh);
		}
		{
			shared_ptr<Shader> shader = GET_SINGLE(Resources)->Get<Shader>(L"Texture");

			shared_ptr<Texture> texture;
			if (i < 3)
				texture = GEngine->GetRTGroup(RENDER_TARGET_GROUP_TYPE::G_BUFFER)->GetRTTexture(i);
			else if (i < 5)
				texture = GEngine->GetRTGroup(RENDER_TARGET_GROUP_TYPE::LIGHTING)->GetRTTexture(i - 3);
			else if (i < 6)
				texture = GEngine->GetRTGroup(RENDER_TARGET_GROUP_TYPE::SHADOW)->GetRTTexture(0);
			else
				texture = GEngine->GetRTGroup(RENDER_TARGET_GROUP_TYPE::BLOOM_HALF)->GetRTTexture(0);

			shared_ptr<Material> material = make_shared<Material>();
			material->SetShader(shader);
			material->SetTexture(0, texture);
			meshRenderer->SetMaterial(material);
		}
		obj->AddComponent(meshRenderer);
		scene->AddGameObject(obj);
	}
#pragma endregion

#pragma region Directional Light
	{
		shared_ptr<GameObject> light = make_shared<GameObject>();
		light->AddComponent(make_shared<Transform>());
		light->GetTransform()->SetLocalPosition(Vec3(0, 1000, 500));
		light->AddComponent(make_shared<Light>());
		light->GetLight()->SetLightDirection(Vec3(0.45f, -0.42f, 1.f));
		light->GetLight()->SetLightType(LIGHT_TYPE::DIRECTIONAL_LIGHT);
		light->GetLight()->SetDiffuse(Vec3(1.f, 1.f, 1.f));
		light->GetLight()->SetAmbient(Vec3(0.1f, 0.1f, 0.1f));
		light->GetLight()->SetSpecular(Vec3(0.1f, 0.1f, 0.1f));

		scene->AddGameObject(light);
	}
#pragma endregion

#pragma region Point Light
	// HDR 로 바꾸기 전에는 조명을 늘려도 곧바로 흰색에서 잘려서 의미가 없었다.
	// 이제 1 을 넘는 밝기가 그대로 남으므로, 세기를 1 이상으로 줘서
	// 블룸이 물어갈 만큼 밝은 지점을 만든다.
	{
		struct PointLightDesc { Vec3 position; Vec3 diffuse; float range; };

		const PointLightDesc descs[] =
		{
			{ Vec3( 250.f, -20.f, 460.f), Vec3(2.4f, 0.85f, 0.22f), 520.f }, // 주황 - 화톳불 자리
			{ Vec3( 330.f,  40.f, 240.f), Vec3(0.20f, 0.60f, 1.8f), 420.f }, // 파랑
			{ Vec3(  90.f, 250.f, 120.f), Vec3(1.2f, 0.25f, 1.1f),  380.f }, // 보라
		};

		for (const PointLightDesc& desc : descs)
		{
			shared_ptr<GameObject> light = make_shared<GameObject>();
			light->AddComponent(make_shared<Transform>());
			light->GetTransform()->SetLocalPosition(desc.position);
			light->AddComponent(make_shared<Light>());
			light->GetLight()->SetLightType(LIGHT_TYPE::POINT_LIGHT);
			light->GetLight()->SetDiffuse(desc.diffuse);
			light->GetLight()->SetAmbient(Vec3(0.f, 0.f, 0.f));
			light->GetLight()->SetSpecular(desc.diffuse * 0.25f);
			light->GetLight()->SetLightRange(desc.range);

			scene->AddGameObject(light);
		}
	}
#pragma endregion


#pragma region FBX
	{
		// NOTE: Dragon.fbx takes ~3 minutes to load in a Debug build (FBX SDK is slow
		// without optimizations). Wrap this region in #if 0 while iterating in Debug.
		shared_ptr<MeshData> meshData = GET_SINGLE(Resources)->LoadFBX(L"..\\Resources\\FBX\\Dragon.fbx");

		vector<shared_ptr<GameObject>> gameObjects = meshData->Instantiate();

		for (auto& gameObject : gameObjects)
		{
			gameObject->SetName(L"Dragon");
			// 바운딩 구를 메시 정점에서 재게 된 뒤로 절두체 컬링을 켤 수 있다.
			// 예전에는 트랜스폼의 로컬 스케일을 반지름으로 썼기 때문에
			// 켜는 순간 화면 가장자리에서 사라졌다.
			gameObject->SetCheckFrustum(true);
			gameObject->GetTransform()->SetLocalPosition(
				Vec3(0.f, terrain->GetHeight(0.f, 300.f), 300.f));
			gameObject->GetTransform()->SetLocalScale(Vec3(1.f, 1.f, 1.f));
			scene->AddGameObject(gameObject);
		}
	}
#pragma endregion

#pragma region BinModel
	{
		shared_ptr<MeshData> meshData = GET_SINGLE(Resources)->LoadBin(L"../Resources/Model/Player.bin");
		vector<shared_ptr<GameObject>> parts = meshData->Instantiate();

		// .bin models are authored in Unity units (Player.bin is ~1.76 tall) while this
		// scene works in hundreds of units, so scale up to make it visible.
		const float scale = 100.f;

		// The model comes in as one GameObject per mesh, so make a root to hold the
		// movement and let the parts follow through Transform parenting.
		shared_ptr<GameObject> player = make_shared<GameObject>();
		player->SetName(L"Player");
		player->AddComponent(make_shared<Transform>());
		player->SetCheckFrustum(false);
		player->SetStatic(false);

		shared_ptr<Transform> playerTransform = player->GetTransform();
		playerTransform->SetLocalPosition(
			Vec3(90.f, terrain->GetHeight(90.f, 340.f), 340.f));
		playerTransform->SetLocalScale(Vec3(scale, scale, scale));
		// The model faces +Z, so without this we only ever see its back.
		playerTransform->SetLocalRotation(Vec3(0.f, XM_PI, 0.f));

		shared_ptr<PlayerController> controller = make_shared<PlayerController>();
		controller->SetFacing(XM_PI);
		// Root translation is in world units; the 100x scale does not apply to it.
		controller->SetMoveSpeed(250.f);
		controller->SetGroundQuery([terrain](float x, float z) { return terrain->GetHeight(x, z); });

		// 지형 가장자리 안쪽으로만 다닌다. 밖에는 설 곳이 없다.
		{
			const TerrainDesc& td = terrain->GetDesc();
			const float half = td.worldSize * 0.5f - 200.f;
			controller->SetMoveBounds(Vec2(td.center.x, td.center.z), Vec2(half, half));
		}

		for (auto& part : parts)
		{
			part->SetName(L"PlayerPart");
			part->SetCheckFrustum(true);
			part->SetStatic(false);
			part->GetTransform()->SetParent(playerTransform);

			controller->AddPart(part);
			scene->AddGameObject(part);
		}

		player->AddComponent(controller);
		scene->AddGameObject(player);

		if (mainCamera)
		{
			shared_ptr<FollowCamera> follow = make_shared<FollowCamera>();
			follow->SetTarget(player);
			follow->SetOffset(Vec3(0.f, 130.f, -330.f));
			follow->SetLookHeight(90.f);
			mainCamera->AddComponent(follow);
		}
	}
#pragma endregion

	return scene;
}