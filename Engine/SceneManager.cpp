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
	// TODO : ï¿½ï¿½ï¿½ï¿½ Scene ï¿½ï¿½ï¿½ï¿½
	// TODO : ï¿½ï¿½ï¿½Ï¿ï¿½ï¿½ï¿½ Scene ï¿½ï¿½ï¿½ï¿½ ï¿½Îµï¿½

	_activeScene = LoadTestScene();

	_activeScene->Awake();
	_activeScene->Start();
}

void SceneManager::SetLayerName(uint8 index, const wstring& name)
{
	// ï¿½ï¿½ï¿½ï¿½ ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ ï¿½ï¿½ï¿½ï¿½
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

	// ViewSpaceï¿½ï¿½ï¿½ï¿½ Picking ï¿½ï¿½ï¿½ï¿½
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

		// ViewSpaceï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ Ray ï¿½ï¿½ï¿½ï¿½
		Vec4 rayOrigin = Vec4(0.0f, 0.0f, 0.0f, 1.0f);
		Vec4 rayDir = Vec4(viewX, viewY, 1.0f, 0.0f);

		// WorldSpaceï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ Ray ï¿½ï¿½ï¿½ï¿½
		rayOrigin = XMVector3TransformCoord(rayOrigin, viewMatrixInv);
		rayDir = XMVector3TransformNormal(rayDir, viewMatrixInv);
		rayDir.Normalize();

		// WorldSpaceï¿½ï¿½ï¿½ï¿½ ï¿½ï¿½ï¿½ï¿½
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

#pragma region ComputeShader
	{
		shared_ptr<Shader> shader = GET_SINGLE(Resources)->Get<Shader>(L"ComputeShader");

		// UAV ï¿½ï¿½ Texture ï¿½ï¿½ï¿½ï¿½
		shared_ptr<Texture> texture = GET_SINGLE(Resources)->CreateTexture(L"UAVTexture",
			DXGI_FORMAT_R8G8B8A8_UNORM, 1024, 1024,
			CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE,
			D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

		shared_ptr<Material> material = GET_SINGLE(Resources)->Get<Material>(L"ComputeShader");
		material->SetShader(shader);
		material->SetInt(0, 1);
		GEngine->GetComputeDescHeap()->SetUAV(texture->GetUAVHandle(), UAV_REGISTER::u0);

		// ï¿½ï¿½ï¿½ï¿½ï¿½ï¿½ ï¿½×·ï¿½ (1 * 1024 * 1)
		material->Dispatch(1, 1024, 1);
	}
#pragma endregion

	shared_ptr<Scene> scene = make_shared<Scene>();

	// Kept outside the block so the follow camera can be wired to the player below.
	shared_ptr<GameObject> mainCamera;
	
#pragma region Camera
	{
		shared_ptr<GameObject> camera = make_shared<GameObject>();
		camera->SetName(L"Main_Camera");
		camera->AddComponent(make_shared<Transform>());
		camera->AddComponent(make_shared<Camera>()); // Near=1, Far=1000, FOV=45ï¿½ï¿½
		// TestCameraScript moves the camera with WASD, which fights the player for
		// the same keys. A follow camera is attached after the player is created.
		mainCamera = camera;
		camera->GetCamera()->SetFar(10000.f);
		camera->GetTransform()->SetLocalPosition(Vec3(0.f, 0.f, 0.f));
		uint8 layerIndex = GET_SINGLE(SceneManager)->LayerNameToIndex(L"UI");
		camera->GetCamera()->SetCullingMaskLayerOnOff(layerIndex, true); // UIï¿½ï¿½ ï¿½ï¿½ ï¿½ï¿½ï¿½ï¿½
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
		camera->GetCamera()->SetCullingMaskAll(); // ï¿½ï¿½ ï¿½ï¿½ï¿½ï¿½
		camera->GetCamera()->SetCullingMaskLayerOnOff(layerIndex, false); // UIï¿½ï¿½ ï¿½ï¿½ï¿½ï¿½
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
			shared_ptr<Texture> texture = GET_SINGLE(Resources)->LoadColorTexture(L"Sky01", L"..\\Resources\\Texture\\Sky01.jpg");
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
	/*{
		shared_ptr<GameObject> obj = make_shared<GameObject>();
		obj->AddComponent(make_shared<Transform>());
		obj->AddComponent(make_shared<Terrain>());
		obj->AddComponent(make_shared<MeshRenderer>());

		obj->GetTransform()->SetLocalScale(Vec3(50.f, 250.f, 50.f));
		obj->GetTransform()->SetLocalPosition(Vec3(-100.f, -200.f, 300.f));
		obj->SetStatic(true);
		obj->GetTerrain()->Init(64, 64);
		obj->SetCheckFrustum(false);

		scene->AddGameObject(obj);
	}*/
#pragma endregion

#pragma region Ground
	// ¹Ù´Ú. Ä³¸¯ÅÍ°¡ Çã°ø¿¡ ¶° ÀÖ¾î¼­ ÆÄÆ¼Å¬ÀÌ ¹«¾ù°ú ¸¸³ª´ÂÁöµµ º¸ÀÌÁö ¾Ê¾Ò´Ù.
	// ¼ÒÇÁÆ® ÆÄÆ¼Å¬Àº "µÚ¿¡ ÀÖ´Â ¹°Ã¼"¿ÍÀÇ ±íÀÌ Â÷·Î ÆäÀÌµåÇÏ´Â °Å¶ó
	// ¾ÖÃÊ¿¡ ¶Õ°í µé¾î°¥ ¸éÀÌ ¾øÀ¸¸é È®ÀÎÇÒ ¼ö°¡ ¾ø´Ù.
	{
		shared_ptr<GameObject> obj = make_shared<GameObject>();
		obj->SetName(L"Ground");
		obj->AddComponent(make_shared<Transform>());
		obj->GetTransform()->SetLocalScale(Vec3(4000.f, 20.f, 4000.f));
		obj->GetTransform()->SetLocalPosition(Vec3(90.f, -95.f, 340.f));
		obj->SetStatic(true);
		obj->SetCheckFrustum(false);

		shared_ptr<MeshRenderer> meshRenderer = make_shared<MeshRenderer>();
		meshRenderer->SetMesh(GET_SINGLE(Resources)->LoadCubeMesh());
		{
			// 4000 À¯´ÖÂ¥¸® ¸é¿¡ ÅØ½ºÃ³¸¦ ÇÑ Àå¸¸ ±ò¸é °¡Á× ¹«´ÌÀÇ ¾ó·èÀÌ
			// °Å´ëÇÑ Èò ¹ÝÁ¡À¸·Î ´Ã¾î³­´Ù. Àß°Ô ¹Ýº¹½ÃÅ²´Ù.
			shared_ptr<Material> material = GET_SINGLE(Resources)->Get<Material>(L"GameObject")->Clone();
			material->SetVec2(0, Vec2(24.f, 24.f));
			meshRenderer->SetMaterial(material);
		}
		obj->AddComponent(meshRenderer);

		scene->AddGameObject(obj);
	}
#pragma endregion

#pragma region Foliage
	// ÀÎ½ºÅÏ½Ì ÃÊ¸ñ.
	// Á¡ ÇÏ³ª¸¦ GS ¿¡¼­ »ç°¢ÇüÀ¸·Î ÆîÄ¡¹Ç·Î Á¤Á¡ ¹öÆÛ¿¡´Â Á¡ ÇÏ³ª»ÓÀÌ°í,
	// À§Ä¡¡¤Å©±â¡¤¹Ù¶÷ À§»óÀº StructuredBuffer ¿¡¼­ ÀÎ½ºÅÏ½º ID ·Î ´ç°Ü¿Â´Ù.
	// ¼öÃµ ÀåÀÌ µå·Î¿ìÄÝ ÇÏ³ª·Î ³ª°£´Ù.
	{
		shared_ptr<GameObject> obj = make_shared<GameObject>();
		obj->SetName(L"Grass");
		obj->AddComponent(make_shared<Transform>());
		obj->SetCheckFrustum(false);

		BillboardDesc desc;
		desc.count = 30000;
		desc.center = Vec3(90.f, 0.f, 340.f);
		desc.area = Vec2(2600.f, 2600.f);
		desc.groundY = -85.f;			// ¹Ù´Ú À­¸é
		desc.minScale = 20.f;
		desc.maxScale = 42.f;
		desc.heightRatio = 1.5f;
		desc.holeCenter = Vec3(250.f, 0.f, 460.f);	// È­ÅêºÒ ÀÚ¸®´Â ºñ¿öµÐ´Ù
		desc.holeRadius = 130.f;
		desc.windDirection = Vec3(1.f, 0.f, 0.35f);
		desc.windStrength = 6.f;
		desc.windFrequency = 1.5f;
		// ½ÉÀº ¹üÀ§(2600)º¸´Ù Âª°Ô ÀÚ¸£¸é Ç®¹ç °¡ÀåÀÚ¸®¿¡ ¿øÇü °æ°è°¡ ´«¿¡ º¸ÀÎ´Ù.
		// ÆäÀÌµå ¾øÀÌ ÀÚ¸¦ °Å¸é ¾Æ¿¹ ¾È ÀÚ¸£´Â ÆíÀÌ ³´´Ù.
		desc.maxDrawDistance = 0.f;

		shared_ptr<BillboardRenderer> billboard = make_shared<BillboardRenderer>();
		billboard->SetDesc(desc);
		obj->AddComponent(billboard);

		scene->AddGameObject(obj);
	}
#pragma endregion

#pragma region Particle
	// È­ÅêºÒ. Áö±Ý±îÁö ºí·ëÀÌ ¹° ¼ö ÀÖ´Â °Ç ¹à°Ô Á¶¸í¹ÞÀº Ç¥¸é»ÓÀÌ¾ú´Ù.
	// È­¸é ¾È¿¡ ½ÇÁ¦·Î ºû³ª´Â °ÍÀ» µÎ¸é ±×¶§ºÎÅÍ "ºû ¹øÁü"ÀÌ µÈ´Ù.
	{
		shared_ptr<GameObject> obj = make_shared<GameObject>();
		obj->SetName(L"Campfire");
		obj->AddComponent(make_shared<Transform>());
		obj->GetTransform()->SetLocalPosition(Vec3(250.f, -85.f, 460.f));
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
		desc.emissive = 2.2f;					// HDR ¹üÀ§·Î ¿Ã·Á ºí·ëÀÌ ¹°°Ô ÇÑ´Ù
		desc.shape = PARTICLE_EMITTER_SHAPE::CONE;
		desc.radius = 20.f;
		desc.coneAngle = 0.42f;
		desc.gravity = Vec3(0.f, 45.f, 0.f);	// ¿­±â¿¡ ¶°¿À¸¥´Ù
		desc.drag = 1.2f;
		desc.maxSpin = 2.0f;
		desc.softFadeDistance = 40.f;
		desc.additive = true;

		shared_ptr<ParticleSystem> particle = make_shared<ParticleSystem>();
		particle->SetDesc(desc);
		obj->AddComponent(particle);

		scene->AddGameObject(obj);
	}

	// ºÒÆ¼. ¿Ã¶ó°¬´Ù Áß·Â¿¡ ¶³¾îÁø´Ù.
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
		desc.textureKey = L"ParticleSpark";
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
		light->GetLight()->SetLightDirection(Vec3(0, -1, 1.f));
		light->GetLight()->SetLightType(LIGHT_TYPE::DIRECTIONAL_LIGHT);
		light->GetLight()->SetDiffuse(Vec3(1.f, 1.f, 1.f));
		light->GetLight()->SetAmbient(Vec3(0.1f, 0.1f, 0.1f));
		light->GetLight()->SetSpecular(Vec3(0.1f, 0.1f, 0.1f));

		scene->AddGameObject(light);
	}
#pragma endregion

#pragma region Point Light
	// HDR ·Î ¹Ù²Ù±â Àü¿¡´Â Á¶¸íÀ» ´Ã·Áµµ °ð¹Ù·Î Èò»ö¿¡¼­ Àß·Á¼­ ÀÇ¹Ì°¡ ¾ø¾ú´Ù.
	// ÀÌÁ¦ 1 À» ³Ñ´Â ¹à±â°¡ ±×´ë·Î ³²À¸¹Ç·Î, ¼¼±â¸¦ 1 ÀÌ»óÀ¸·Î Áà¼­
	// ºí·ëÀÌ ¹°¾î°¥ ¸¸Å­ ¹àÀº ÁöÁ¡À» ¸¸µç´Ù.
	{
		struct PointLightDesc { Vec3 position; Vec3 diffuse; float range; };

		const PointLightDesc descs[] =
		{
			{ Vec3( 250.f, -20.f, 460.f), Vec3(2.4f, 0.85f, 0.22f), 520.f }, // ÁÖÈ² - È­ÅêºÒ ÀÚ¸®
			{ Vec3( 330.f,  40.f, 240.f), Vec3(0.20f, 0.60f, 1.8f), 420.f }, // ÆÄ¶û
			{ Vec3(  90.f, 250.f, 120.f), Vec3(1.2f, 0.25f, 1.1f),  380.f }, // º¸¶ó
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
			gameObject->SetCheckFrustum(false);
			gameObject->GetTransform()->SetLocalPosition(Vec3(0.f, 0.f, 300.f));
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
		playerTransform->SetLocalPosition(Vec3(90.f, -85.f, 340.f));
		playerTransform->SetLocalScale(Vec3(scale, scale, scale));
		// The model faces +Z, so without this we only ever see its back.
		playerTransform->SetLocalRotation(Vec3(0.f, XM_PI, 0.f));

		shared_ptr<PlayerController> controller = make_shared<PlayerController>();
		controller->SetFacing(XM_PI);
		// Root translation is in world units; the 100x scale does not apply to it.
		controller->SetMoveSpeed(250.f);

		for (auto& part : parts)
		{
			part->SetName(L"PlayerPart");
			part->SetCheckFrustum(false);
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