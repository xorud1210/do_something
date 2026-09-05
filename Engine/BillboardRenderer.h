#pragma once
#include "Component.h"

class Material;
class Mesh;
class StructuredBuffer;

// 빌보드 하나의 인스턴스 데이터.
// billboard.fx 의 BillboardInstance 와 배치가 같아야 한다.
struct BillboardInstance
{
	Vec3	worldPos;
	float	scale;
	float	phase;			// 인스턴스마다 다른 바람 위상
	Vec3	padding;
};

struct BillboardDesc
{
	uint32	count = 2000;

	// XZ 평면에 흩뿌릴 범위. groundY 는 밑동이 놓일 높이다.
	Vec3	center = Vec3(0.f, 0.f, 0.f);
	Vec2	area = Vec2(2000.f, 2000.f);
	float	groundY = 0.f;

	float	minScale = 40.f;
	float	maxScale = 80.f;
	float	heightRatio = 1.4f;		// 가로 대비 세로

	// 카메라에서 이만큼 안쪽에는 심지 않는다. 플레이어 발밑이 풀에 파묻히는 걸 막는다.
	Vec3	holeCenter = Vec3(0.f, 0.f, 0.f);
	float	holeRadius = 0.f;

	Vec3	windDirection = Vec3(1.f, 0.f, 0.35f);
	float	windStrength = 10.f;
	float	windFrequency = 1.6f;

	wstring	textureKey = L"FoliageGrass";
	wstring	texturePath = L"..\\Resources\\Texture\\Foliage\\grass.png";

	uint32	seed = 1;
};

// 점 하나를 GS 에서 사각형으로 펼치는 축 고정 빌보드.
//
// 인스턴스 데이터를 StructuredBuffer 에 올려두고 정점 셰이더가 인스턴스 ID 로
// 당겨온다. 정점 버퍼에는 점 하나뿐이라 수천 장이 드로우콜 하나로 나간다.
class BillboardRenderer : public Component
{
public:
	BillboardRenderer();
	virtual ~BillboardRenderer();

public:
	virtual void FinalUpdate() override;
	void Render();

	// 설정에 맞춰 인스턴스를 흩뿌리고 버퍼를 다시 만든다.
	void SetDesc(const BillboardDesc& desc);
	const BillboardDesc& GetDesc() const { return _desc; }

	uint32 GetInstanceCount() const { return _instanceCount; }

public:
	virtual void Load(const wstring& path) override { }
	virtual void Save(const wstring& path) override { }

private:
	BillboardDesc					_desc;

	shared_ptr<StructuredBuffer>	_instanceBuffer;
	uint32							_instanceCount = 0;

	shared_ptr<Material>			_material;
	shared_ptr<Mesh>				_mesh;

	float							_accTime = 0.f;
};
