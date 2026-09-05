#pragma once
#include "Component.h"

class Material;
class Mesh;
class StructuredBuffer;

// 빌보드 하나의 인스턴스 데이터.
// billboard.fx / foliage_cull.fx 의 BillboardInstance 와 배치가 같아야 한다.
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

	// 이보다 먼 것은 그리지 않는다. 0 이면 거리 제한 없음.
	float	maxDrawDistance = 0.f;

	// 셰도우 맵에도 그릴 것인가.
	// 캐스케이드마다 한 번씩 더 그리므로 공짜가 아니다.
	bool	castShadow = false;

	// 앞에서 몇 개의 캐스케이드까지 그릴 것인가 (SHADOW_CASCADE_COUNT 가 상한).
	//
	// 처음에는 "먼 구간에서는 풀 한 장이 텍셀보다 작아지니 빼자" 고 생각했는데,
	// 재 보니 틀렸다. 가장 먼 구간에서도 텍셀 하나가 월드 2.2 단위라
	// 폭 20~42 짜리 풀은 9~19 텍셀을 덮는다. 셰도우 맵은 충분히 담아낸다.
	// 줄이는 것은 순수한 비용/품질 다이얼이다 - 먼 풀밭이 평평해지는 대신 싸진다.
	uint32	shadowCascadeCount = 3;

	wstring	textureKey = L"FoliageGrass";
	wstring	texturePath = L"..\\Resources\\Texture\\Foliage\\grass.png";

	uint32	seed = 1;
};

// 점 하나를 GS 에서 사각형으로 펼치는 축 고정 빌보드.
//
// 인스턴스 데이터를 StructuredBuffer 에 올려두고 정점 셰이더가 인스턴스 ID 로
// 당겨온다. 정점 버퍼에는 점 하나뿐이라 수천 장이 드로우콜 하나로 나간다.
//
// 그 드로우콜의 인스턴스 개수는 CPU 가 정하지 않는다.
// 컴퓨트 셰이더가 절두체 밖의 것을 걸러 통과한 것만 별도 버퍼에 모으고,
// 그 개수를 그리기 인자 버퍼에 직접 써 넣는다. 그리기는 ExecuteIndirect 로
// 그 버퍼를 가리키기만 한다.
class BillboardRenderer : public Component
{
public:
	BillboardRenderer();
	virtual ~BillboardRenderer();

public:
	virtual void FinalUpdate() override;
	void Render();

	// 셰도우 패스. 캐스케이드마다 한 번씩 불린다.
	void RenderShadow(uint32 cascade);
	bool IsCastShadow() const { return s_shadowEnabled && _desc.castShadow && _instanceCount > 0; }

	// 설정에 맞춰 인스턴스를 흩뿌리고 버퍼를 다시 만든다.
	void SetDesc(const BillboardDesc& desc);
	const BillboardDesc& GetDesc() const { return _desc; }

	uint32 GetInstanceCount() const { return _instanceCount; }
	uint32 GetVisibleCount() const { return _visibleCount; }

	// 컬링을 켜고 끈다. 대조 스크린샷과 성능 비교용 (F2).
	static void SetCullEnabled(bool value) { s_cullEnabled = value; }
	static bool IsCullEnabled() { return s_cullEnabled; }

	// 초목 그림자를 켜고 끈다 (F3). 대조 스크린샷용.
	static void SetShadowEnabled(bool value) { s_shadowEnabled = value; }
	static bool IsShadowEnabled() { return s_shadowEnabled; }

public:
	virtual void Load(const wstring& path) override { }
	virtual void Save(const wstring& path) override { }

private:
	// 절두체 컬링 자원을 만든다. 인스턴스 개수가 정해진 뒤에 부른다.
	void CreateCullResources();

	// 컴퓨트 큐에서 컬링을 돌리고 통과 개수를 되읽는다.
	void CullOnGPU();

private:
	BillboardDesc					_desc;

	shared_ptr<StructuredBuffer>	_instanceBuffer;	// 원본. 심을 때 한 번 만들고 안 바뀐다
	uint32							_instanceCount = 0;

	// --- GPU 컬링 ---
	shared_ptr<StructuredBuffer>	_visibleBuffer;		// 통과한 것만 추린 목록
	shared_ptr<StructuredBuffer>	_argsBuffer;		// D3D12_DRAW_INDEXED_ARGUMENTS
	ComPtr<ID3D12Resource>			_argsReset;			// 매 프레임 되돌릴 초기값 (UPLOAD)
	ComPtr<ID3D12Resource>			_argsReadback;		// 통과 개수 확인용 (READBACK)
	uint32*							_readback = nullptr;
	shared_ptr<Material>			_cullMaterial;
	uint32							_visibleCount = 0;

	shared_ptr<Material>			_material;
	shared_ptr<Material>			_shadowMaterial;
	shared_ptr<Mesh>				_mesh;

	float							_accTime = 0.f;

private:
	// 커맨드 시그니처는 "인자 버퍼를 어떻게 읽을 것인가" 만 담는다.
	// 메시나 머티리얼과 무관하므로 하나만 만들어 공유한다.
	static ComPtr<ID3D12CommandSignature>	s_cmdSignature;
	static bool								s_cullEnabled;
	static bool								s_shadowEnabled;
};
