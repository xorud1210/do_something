#include "pch.h"
#include "Light.h"
#include "Transform.h"
#include "Engine.h"
#include "Resources.h"
#include "Camera.h"
#include "Transform.h"
#include "Texture.h"
#include "SceneManager.h"
#include "Scene.h"
#include "Input.h"
#include "Material.h"
#include "Mesh.h"

Light::Light() : Component(COMPONENT_TYPE::LIGHT)
{
	_shadowCamera = make_shared<GameObject>();
	_shadowCamera->AddComponent(make_shared<Transform>());
	_shadowCamera->AddComponent(make_shared<Camera>());
	uint8 layerIndex = GET_SINGLE(SceneManager)->LayerNameToIndex(L"UI");
	_shadowCamera->GetCamera()->SetCullingMaskLayerOnOff(layerIndex, true); // UI는 안 찍음
}

Light::~Light()
{
}

void Light::FinalUpdate()
{
	if (INPUT->GetButtonDown(KEY_TYPE::F1))
		_cascadeDebug = !_cascadeDebug;

	_lightInfo.position = GetTransform()->GetWorldPosition();

	_shadowCamera->GetTransform()->SetLocalPosition(GetTransform()->GetLocalPosition());
	_shadowCamera->GetTransform()->SetLocalRotation(GetTransform()->GetLocalRotation());
	_shadowCamera->GetTransform()->SetLocalScale(GetTransform()->GetLocalScale());

	_shadowCamera->FinalUpdate();
}

void Light::Render()
{
	assert(_lightIndex >= 0);

	GetTransform()->PushData();

	if (static_cast<LIGHT_TYPE>(_lightInfo.lightType) == LIGHT_TYPE::DIRECTIONAL_LIGHT)
	{
		shared_ptr<Texture> shadowTex = GET_SINGLE(Resources)->Get<Texture>(L"ShadowTarget");
		_lightMaterial->SetTexture(2, shadowTex);

		// 캐스케이드마다 행렬이 다르다. 픽셀 셰이더가 뷰 깊이로 하나를 고른다.
		_lightMaterial->SetInt(1, static_cast<int32>(SHADOW_CASCADE_COUNT));
		for (uint32 c = 0; c < SHADOW_CASCADE_COUNT; c++)
			_lightMaterial->SetMatrix(static_cast<uint8>(c), _cascadeVP[c]);

		_lightMaterial->SetVec4(0, Vec4(_cascadeSplit[0], _cascadeSplit[1], _cascadeSplit[2], 0.f));
		_lightMaterial->SetVec4(1, Vec4(_cascadeTexelWorld[0], _cascadeTexelWorld[1], _cascadeTexelWorld[2], 0.f));
		_lightMaterial->SetFloat(0, _shadowBias);
		_lightMaterial->SetFloat(1, 1.f / static_cast<float>(SHADOW_MAP_SIZE));
		_lightMaterial->SetFloat(3, _cascadeBlend);
		_lightMaterial->SetInt(2, _cascadeDebug ? 1 : 0);
	}
	else
	{
		float scale = 2 * _lightInfo.range;
		GetTransform()->SetLocalScale(Vec3(scale, scale, scale));
	}

	_lightMaterial->SetInt(0, _lightIndex);
	_lightMaterial->PushGraphicsData();

	_volumeMesh->Render();
}

void Light::RenderShadow()
{
	if (GetLightType() != LIGHT_TYPE::DIRECTIONAL_LIGHT)
		return;

	UpdateCascades();

	shared_ptr<Camera> shadowCamera = _shadowCamera->GetCamera();
	shadowCamera->SortShadowObject();

	for (uint32 c = 0; c < SHADOW_CASCADE_COUNT; c++)
	{
		// 아틀라스의 해당 타일에만 그리도록 뷰포트와 시저를 옮긴다.
		// 렌더타겟을 여러 장 두는 대신 한 장을 나눠 쓰면 SRV 도 하나로 끝난다.
		const float tileX = static_cast<float>((c % 2) * SHADOW_TILE_SIZE);
		const float tileY = static_cast<float>((c / 2) * SHADOW_TILE_SIZE);
		const float tileSize = static_cast<float>(SHADOW_TILE_SIZE);

		D3D12_VIEWPORT viewport = { tileX, tileY, tileSize, tileSize, 0.f, 1.f };
		D3D12_RECT rect =
		{
			static_cast<LONG>(tileX), static_cast<LONG>(tileY),
			static_cast<LONG>(tileX + tileSize), static_cast<LONG>(tileY + tileSize)
		};

		GRAPHICS_CMD_LIST->RSSetViewports(1, &viewport);
		GRAPHICS_CMD_LIST->RSSetScissorRects(1, &rect);

		shadowCamera->SetViewMatrix(_cascadeView[c]);
		shadowCamera->SetProjectionMatrix(_cascadeProj[c]);
		shadowCamera->Render_Shadow(c);
	}
}

void Light::UpdateCascades()
{
	shared_ptr<Scene> scene = GET_SINGLE(SceneManager)->GetActiveScene();
	if (scene == nullptr)
		return;

	shared_ptr<Camera> mainCamera = scene->GetMainCamera();
	if (mainCamera == nullptr)
		return;

	const float nearZ = mainCamera->GetNear();
	const float cameraFar = mainCamera->GetFar();
	const float farZ = (cameraFar < _shadowDistance) ? cameraFar : _shadowDistance;
	const float fov = mainCamera->GetFOV();
	const float aspect = mainCamera->GetWidth() / mainCamera->GetHeight();

	Matrix matViewInv = mainCamera->GetViewMatrix().Invert();

	Vec3 lightDir = Vec3(_lightInfo.direction.x, _lightInfo.direction.y, _lightInfo.direction.z);
	lightDir.Normalize();

	// 구간 나누기. 로그 분할은 가까운 쪽에 몰리고 균등 분할은 고르게 퍼진다.
	// 둘을 섞어 쓰는 게 관행이다.
	float splits[SHADOW_CASCADE_COUNT + 1];
	splits[0] = nearZ;
	for (uint32 i = 1; i <= SHADOW_CASCADE_COUNT; i++)
	{
		const float t = static_cast<float>(i) / static_cast<float>(SHADOW_CASCADE_COUNT);
		const float logSplit = nearZ * ::powf(farZ / nearZ, t);
		const float uniformSplit = nearZ + (farZ - nearZ) * t;
		splits[i] = _cascadeLambda * logSplit + (1.f - _cascadeLambda) * uniformSplit;
	}

	const float tanHalfV = ::tanf(fov * 0.5f);
	const float tanHalfH = tanHalfV * aspect;

	// 빛이 거의 수직이면 up 이 방향과 나란해져 LookAt 이 무너진다.
	Vec3 up = (::fabsf(lightDir.y) > 0.99f) ? Vec3(0.f, 0.f, 1.f) : Vec3(0.f, 1.f, 0.f);

	// 절두체 뒤쪽에 있는 물체도 그림자를 던진다. 광원을 그만큼 뒤로 물린다.
	const float backOffset = 2000.f;

	for (uint32 c = 0; c < SHADOW_CASCADE_COUNT; c++)
	{
		const float n = splits[c];
		const float f = splits[c + 1];
		_cascadeSplit[c] = f;

		// 이 구간 절두체의 꼭짓점 8개 (카메라 뷰 공간)
		Vec3 cornersView[8];
		int32 k = 0;
		const float zs[2] = { n, f };
		const float signs[2] = { -1.f, 1.f };
		for (int32 zi = 0; zi < 2; zi++)
			for (int32 yi = 0; yi < 2; yi++)
				for (int32 xi = 0; xi < 2; xi++)
				{
					const float z = zs[zi];
					cornersView[k++] = Vec3(signs[xi] * tanHalfH * z, signs[yi] * tanHalfV * z, z);
				}

		// 경계를 AABB 가 아니라 구로 잡는다.
		// 구의 반지름은 카메라가 어느 방향을 보든 변하지 않으므로,
		// 카메라를 돌려도 그림자가 커졌다 작아졌다 떨지 않는다.
		Vec3 centerView = Vec3(0.f, 0.f, 0.f);
		for (int32 i = 0; i < 8; i++)
			centerView += cornersView[i];
		centerView /= 8.f;

		float radius = 0.f;
		for (int32 i = 0; i < 8; i++)
		{
			const float d = (cornersView[i] - centerView).Length();
			if (d > radius)
				radius = d;
		}
		radius = ::ceilf(radius * 16.f) / 16.f;

		Vec3 center = Vec3::Transform(centerView, matViewInv);

		// 텍셀 스냅.
		// 스냅하지 않으면 카메라가 조금만 움직여도 셰도우 맵의 격자와 월드가
		// 어긋나면서 그림자 가장자리가 계단 단위로 기어다닌다.
		const float texelWorld = (radius * 2.f) / static_cast<float>(SHADOW_TILE_SIZE);
		_cascadeTexelWorld[c] = texelWorld;

		Matrix snapView = ::XMMatrixLookAtLH(center - lightDir * (radius + backOffset), center, up);
		Vec3 centerLS = Vec3::Transform(center, snapView);
		centerLS.x = ::floorf(centerLS.x / texelWorld) * texelWorld;
		centerLS.y = ::floorf(centerLS.y / texelWorld) * texelWorld;
		center = Vec3::Transform(centerLS, snapView.Invert());

		const Vec3 eye = center - lightDir * (radius + backOffset);
		_cascadeView[c] = ::XMMatrixLookAtLH(eye, center, up);
		_cascadeProj[c] = ::XMMatrixOrthographicLH(radius * 2.f, radius * 2.f,
			1.f, radius * 2.f + backOffset + 500.f);
		_cascadeVP[c] = _cascadeView[c] * _cascadeProj[c];
	}
}


void Light::SetLightDirection(Vec3 direction)
{
	direction.Normalize();

	_lightInfo.direction = direction;

	GetTransform()->LookAt(direction);
}

void Light::SetLightType(LIGHT_TYPE type)
{
	_lightInfo.lightType = static_cast<int32>(type);

	switch (type)
	{
	case LIGHT_TYPE::DIRECTIONAL_LIGHT:
		_volumeMesh = GET_SINGLE(Resources)->Get<Mesh>(L"Rectangle");
		_lightMaterial = GET_SINGLE(Resources)->Get<Material>(L"DirLight");

		_shadowCamera->GetCamera()->SetScale(1.f);
		_shadowCamera->GetCamera()->SetFar(10000.f);
		_shadowCamera->GetCamera()->SetWidth(4096);
		_shadowCamera->GetCamera()->SetHeight(4096);

		break;
	case LIGHT_TYPE::POINT_LIGHT:
		_volumeMesh = GET_SINGLE(Resources)->Get<Mesh>(L"Sphere");
		_lightMaterial = GET_SINGLE(Resources)->Get<Material>(L"PointLight");
		break;
	case LIGHT_TYPE::SPOT_LIGHT:
		_volumeMesh = GET_SINGLE(Resources)->Get<Mesh>(L"Sphere");
		_lightMaterial = GET_SINGLE(Resources)->Get<Material>(L"PointLight");
		break;
	}
}