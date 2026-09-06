#include "pch.h"
#include "ParticleSystem.h"
#include "StructuredBuffer.h"
#include "Material.h"
#include "Mesh.h"
#include "Texture.h"
#include "Resources.h"
#include "Transform.h"
#include "Timer.h"
#include "Engine.h"

ParticleSystem::ParticleSystem() : Component(COMPONENT_TYPE::PARTICLE_SYSTEM)
{
	_mesh = GET_SINGLE(Resources)->LoadPointMesh();

	SetDesc(ParticleDesc());
}

ParticleSystem::~ParticleSystem()
{
}

void ParticleSystem::SetDesc(const ParticleDesc& desc)
{
	_desc = desc;

	if (_desc.maxParticle > PARTICLE_MAX_COUNT)
		_desc.maxParticle = PARTICLE_MAX_COUNT;
	if (_desc.maxParticle == 0)
		_desc.maxParticle = 1;

	CreateBuffers();
	CreateMaterials();
}

void ParticleSystem::CreateBuffers()
{
	// 개수가 그대로면 다시 만들 이유가 없다.
	if (_particleBuffer != nullptr && _bufferCount == _desc.maxParticle)
		return;

	_particleBuffer = make_shared<StructuredBuffer>();
	_particleBuffer->Init(sizeof(ParticleInfo), _desc.maxParticle);

	_computeSharedBuffer = make_shared<StructuredBuffer>();
	_computeSharedBuffer->Init(sizeof(ComputeSharedInfo), 1);

	_bufferCount = _desc.maxParticle;
}

void ParticleSystem::CreateMaterials()
{
	const wstring shaderKey = _desc.additive ? L"ParticleAdditive" : L"Particle";

	// 원본을 복제해서 인스턴스가 각자 들고 있는다.
	// 그냥 Get 해서 쓰면 이미터 두 개가 같은 머티리얼에 서로 다른 값을 써넣는다.
	_material = GET_SINGLE(Resources)->Get<Material>(shaderKey)->Clone();
	_computeMaterial = GET_SINGLE(Resources)->Get<Material>(L"ComputeParticle")->Clone();

	if (_desc.texturePath.empty() == false)
	{
		shared_ptr<Texture> tex = GET_SINGLE(Resources)->LoadTexture(_desc.texturePath, true);
		_material->SetTexture(0, tex);
	}

	// 소프트 파티클은 G-Buffer 의 뷰 공간 위치를 읽어 깊이를 비교한다.
	// 파티클은 포워드 패스라 이 시점에 G-Buffer 는 이미 다 그려져 읽을 수 있다.
	if (_desc.softFadeDistance > 0.f)
	{
		_material->SetTexture(1, GET_SINGLE(Resources)->Get<Texture>(L"PositionTarget"));

		const WindowInfo& window = GEngine->GetWindow();
		_material->SetVec2(0, Vec2(static_cast<float>(window.width),
			static_cast<float>(window.height)));
	}
	else
	{
		_material->SetTexture(1, nullptr);
	}

	_material->SetFloat(0, _desc.startScale);
	_material->SetFloat(1, _desc.endScale);
	_material->SetFloat(2, _desc.emissive);
	_material->SetFloat(3, _desc.softFadeDistance);
	_material->SetVec4(0, _desc.startColor);
	_material->SetVec4(1, _desc.endColor);
}

void ParticleSystem::FinalUpdate()
{
	_accTime += DELTA_TIME;

	// 프레임이 길어졌으면 그만큼 몰아서 태운다.
	// 예전에는 프레임당 최대 하나라 프레임레이트에 방출량이 끌려다녔다.
	int32 add = 0;
	if (_desc.createInterval > 0.f)
	{
		add = static_cast<int32>(_accTime / _desc.createInterval);
		_accTime -= add * _desc.createInterval;

		const int32 cap = static_cast<int32>(_desc.maxParticle);
		if (add > cap)
			add = cap;
	}

	_particleBuffer->PushComputeUAVData(UAV_REGISTER::u0);
	_computeSharedBuffer->PushComputeUAVData(UAV_REGISTER::u1);

	_computeMaterial->SetInt(0, static_cast<int32>(_desc.maxParticle));
	_computeMaterial->SetInt(1, add);
	_computeMaterial->SetInt(2, static_cast<int32>(_desc.shape));

	_computeMaterial->SetVec2(1, Vec2(DELTA_TIME, _accTime));
	_computeMaterial->SetVec4(0, Vec4(_desc.minLifeTime, _desc.maxLifeTime,
		_desc.minSpeed, _desc.maxSpeed));
	_computeMaterial->SetVec4(1, Vec4(_desc.radius, ::cosf(_desc.coneAngle),
		_desc.drag, _desc.maxSpin));
	_computeMaterial->SetVec4(2, Vec4(_desc.gravity.x, _desc.gravity.y, _desc.gravity.z, 0.f));

	_computeMaterial->Dispatch(1, 1, 1);
}

void ParticleSystem::Render()
{
	GetTransform()->PushData();

	_particleBuffer->PushGraphicsData(SRV_REGISTER::t9);
	_material->PushGraphicsData();

	_mesh->Render(_desc.maxParticle);
}
