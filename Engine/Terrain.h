#pragma once
#include "Component.h"

class Material;
class Mesh;

// 하이트맵에서 지형 메시를 만든다.
//
// 졸업작품은 패치를 테셀레이션으로 쪼개 그렸다. 여기서는 CPU 에서 격자를 한 번
// 만들고 끝낸다 - 그러면 디퍼드 패스에 그대로 들어가고, 조명·그림자·안개를
// 다른 오브젝트와 똑같이 받는다. 지형만 별도 경로를 타지 않는다.
//
// 대신 지형이 정적이어야 한다. 이 씬은 그렇다.
struct TerrainDesc
{
	// 하이트맵 (16비트 리틀엔디안 정사각 raw)
	wstring	heightPath = L"..\\Resources\\Terrain\\height513.raw";
	uint32	heightMapSize = 513;

	// 실제로 만들 격자의 한 변 정점 수. 하이트맵보다 성기게 잡아도 된다.
	uint32	gridSize = 257;

	// 월드에서 차지하는 가로/세로 크기와 높이 진폭.
	float	worldSize = 6000.f;
	float	heightScale = 320.f;

	// 지형의 중심. 밑동이 아니라 평균 높이가 이 y 에 오도록 맞춘다.
	Vec3	center = Vec3(0.f, 0.f, 0.f);

	// 디테일 텍스처를 몇 번 반복할 것인가.
	float	detailTiling = 90.f;
};

class Terrain : public Component
{
public:
	Terrain();
	virtual ~Terrain();

	void Init(const TerrainDesc& desc);

	// 월드 좌표 (x, z) 의 지면 높이. 격자 안에서 이중선형 보간한다.
	// 범위 밖이면 desc.center.y 를 준다.
	float GetHeight(float worldX, float worldZ) const;

	// 그 지점의 지면 법선. 나무·바위를 경사에 맞춰 세울 때 쓴다.
	Vec3 GetNormal(float worldX, float worldZ) const;

	const TerrainDesc& GetDesc() const { return _desc; }

public:
	virtual void Load(const wstring& path) override { }
	virtual void Save(const wstring& path) override { }

private:
	// raw 를 읽어 0~1 로 정규화한 격자를 만든다. 실패하면 false.
	bool LoadHeights(const TerrainDesc& desc);

private:
	TerrainDesc		_desc;

	// gridSize x gridSize. 월드 높이(y) 를 그대로 담는다.
	vector<float>	_heights;
	float			_step = 1.f;	// 격자 한 칸의 월드 크기
	Vec3			_origin;		// 격자 (0,0) 의 월드 위치
};
