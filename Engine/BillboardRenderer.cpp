#include "pch.h"
#include "BillboardRenderer.h"
#include "StructuredBuffer.h"
#include "Material.h"
#include "Mesh.h"
#include "Texture.h"
#include "Resources.h"
#include "Transform.h"
#include "Timer.h"

#include <random>

BillboardRenderer::BillboardRenderer() : Component(COMPONENT_TYPE::BILLBOARD_RENDERER)
{
	_mesh = GET_SINGLE(Resources)->LoadPointMesh();
}

BillboardRenderer::~BillboardRenderer()
{
}

void BillboardRenderer::SetDesc(const BillboardDesc& desc)
{
	_desc = desc;

	if (_desc.count == 0)
		_desc.count = 1;

	// 인스턴스를 흩뿌린다.
	// 심는 위치는 한 번 정해지면 바뀌지 않으므로 CPU 에서 만들어 올리고 끝낸다.
	// 흔들림은 셰이더가 시간과 위상으로 만든다.
	std::mt19937 rng(_desc.seed);
	std::uniform_real_distribution<float> unit(0.f, 1.f);

	vector<BillboardInstance> instances;
	instances.reserve(_desc.count);

	const float halfX = _desc.area.x * 0.5f;
	const float halfZ = _desc.area.y * 0.5f;
	const float holeRadiusSq = _desc.holeRadius * _desc.holeRadius;

	// 구멍에 걸린 자리는 버리므로 시도 횟수를 넉넉히 잡는다.
	const uint32 maxTry = _desc.count * 4;

	for (uint32 i = 0; i < maxTry && instances.size() < _desc.count; i++)
	{
		const float x = _desc.center.x + (unit(rng) * 2.f - 1.f) * halfX;
		const float z = _desc.center.z + (unit(rng) * 2.f - 1.f) * halfZ;

		if (holeRadiusSq > 0.f)
		{
			const float dx = x - _desc.holeCenter.x;
			const float dz = z - _desc.holeCenter.z;
			if (dx * dx + dz * dz < holeRadiusSq)
				continue;
		}

		BillboardInstance instance = {};
		instance.worldPos = Vec3(x, _desc.groundY, z);
		instance.scale = _desc.minScale + (_desc.maxScale - _desc.minScale) * unit(rng);
		instance.phase = unit(rng) * 6.2831853f;
		instances.push_back(instance);
	}

	_instanceCount = static_cast<uint32>(instances.size());
	if (_instanceCount == 0)
		return;

	_instanceBuffer = make_shared<StructuredBuffer>();
	_instanceBuffer->Init(sizeof(BillboardInstance), _instanceCount, instances.data());

	// 머티리얼은 인스턴스마다 복제한다.
	// 리소스의 원본을 그대로 쓰면 빌보드 묶음 둘이 서로의 바람 설정을 덮어쓴다.
	_material = GET_SINGLE(Resources)->Get<Material>(L"Billboard")->Clone();

	shared_ptr<Texture> texture = GET_SINGLE(Resources)->Load<Texture>(
		_desc.textureKey, _desc.texturePath);
	_material->SetTexture(0, texture);

	_material->SetFloat(1, _desc.windStrength);
	_material->SetFloat(2, _desc.windFrequency);
	_material->SetFloat(3, _desc.heightRatio);

	Vec3 wind = _desc.windDirection;
	wind.Normalize();
	_material->SetVec4(0, Vec4(wind.x, wind.y, wind.z, 0.f));
}

void BillboardRenderer::FinalUpdate()
{
	_accTime += DELTA_TIME;
}

void BillboardRenderer::Render()
{
	if (_instanceBuffer == nullptr || _instanceCount == 0)
		return;

	GetTransform()->PushData();

	_instanceBuffer->PushGraphicsData(SRV_REGISTER::t9);

	_material->SetFloat(0, _accTime);
	_material->PushGraphicsData();

	// 정점 버퍼에는 점 하나뿐이고, 나머지는 인스턴스 ID 로 버퍼에서 당겨온다.
	// 드로우콜 한 번에 _instanceCount 장이 나간다.
	_mesh->Render(_instanceCount);
}
