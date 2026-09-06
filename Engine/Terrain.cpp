#include "pch.h"
#include "Terrain.h"
#include "Resources.h"
#include "Transform.h"
#include "MeshRenderer.h"
#include "Material.h"
#include "Mesh.h"
#include "Texture.h"

#include <cstdio>

Terrain::Terrain() : Component(COMPONENT_TYPE::TERRAIN)
{
}

Terrain::~Terrain()
{
}

bool Terrain::LoadHeights(const TerrainDesc& desc)
{
	const uint32 src = desc.heightMapSize;
	const uint32 grid = desc.gridSize;
	if (src < 2 || grid < 2)
		return false;

	vector<uint16> raw(static_cast<size_t>(src) * src);

	FILE* file = nullptr;
	::_wfopen_s(&file, desc.heightPath.c_str(), L"rb");
	if (file == nullptr)
		return false;

	const size_t read = ::fread(raw.data(), sizeof(uint16), raw.size(), file);
	::fclose(file);

	if (read != raw.size())
		return false;

	// 16비트 원본을 0~1 로 편다.
	// 절대값을 그대로 쓰면 하이트맵마다 진폭이 제각각이라 씬을 다시 맞춰야 한다.
	uint16 lo = raw[0], hi = raw[0];
	for (uint16 v : raw)
	{
		if (v < lo) lo = v;
		if (v > hi) hi = v;
	}
	const float span = (hi > lo) ? static_cast<float>(hi - lo) : 1.f;

	_heights.assign(static_cast<size_t>(grid) * grid, 0.f);

	// 격자가 하이트맵보다 성기면 건너뛰며 읽는다.
	for (uint32 z = 0; z < grid; z++)
	{
		for (uint32 x = 0; x < grid; x++)
		{
			const float u = static_cast<float>(x) / (grid - 1);
			const float v = static_cast<float>(z) / (grid - 1);

			const uint32 sx = static_cast<uint32>(u * (src - 1) + 0.5f);
			const uint32 sz = static_cast<uint32>(v * (src - 1) + 0.5f);

			const float h = (raw[static_cast<size_t>(sz) * src + sx] - lo) / span;
			_heights[static_cast<size_t>(z) * grid + x] = h;
		}
	}

	// 평균이 center.y 에 오도록 옮긴다. 그래야 씬의 다른 것들과 눈높이가 맞는다.
	float mean = 0.f;
	for (float h : _heights)
		mean += h;
	mean /= static_cast<float>(_heights.size());

	for (float& h : _heights)
		h = (h - mean) * desc.heightScale + desc.center.y;

	return true;
}

void Terrain::Init(const TerrainDesc& desc)
{
	_desc = desc;

	if (LoadHeights(_desc) == false)
		return;

	const uint32 grid = _desc.gridSize;
	_step = _desc.worldSize / (grid - 1);
	_origin = Vec3(_desc.center.x - _desc.worldSize * 0.5f,
		0.f,
		_desc.center.z - _desc.worldSize * 0.5f);

	// --- 정점 ---
	vector<Vertex> vertices(static_cast<size_t>(grid) * grid);

	for (uint32 z = 0; z < grid; z++)
	{
		for (uint32 x = 0; x < grid; x++)
		{
			const size_t i = static_cast<size_t>(z) * grid + x;

			Vertex& v = vertices[i];
			v.pos = Vec3(_origin.x + x * _step, _heights[i], _origin.z + z * _step);

			// UV 는 지형 전체에 0~1. 컬러맵이 이 UV 를 쓰고,
			// 디테일 텍스처는 셰이더에서 여기에 배율을 곱해 반복시킨다.
			v.uv = Vec2(static_cast<float>(x) / (grid - 1),
				static_cast<float>(z) / (grid - 1));
		}
	}

	// --- 법선 ---
	// 이웃 높이의 차이로 구한다. 삼각형 법선을 모아 평균 내는 것과 결과가
	// 거의 같으면서 훨씬 짧다. 가장자리는 자기 자신을 이웃으로 쓴다.
	for (uint32 z = 0; z < grid; z++)
	{
		for (uint32 x = 0; x < grid; x++)
		{
			const uint32 xl = (x > 0) ? x - 1 : x;
			const uint32 xr = (x + 1 < grid) ? x + 1 : x;
			const uint32 zd = (z > 0) ? z - 1 : z;
			const uint32 zu = (z + 1 < grid) ? z + 1 : z;

			const float hl = _heights[static_cast<size_t>(z) * grid + xl];
			const float hr = _heights[static_cast<size_t>(z) * grid + xr];
			const float hd = _heights[static_cast<size_t>(zd) * grid + x];
			const float hu = _heights[static_cast<size_t>(zu) * grid + x];

			const float dx = (xr - xl) * _step;
			const float dz = (zu - zd) * _step;

			Vec3 normal = Vec3(-(hr - hl) * dz, dx * dz, -(hu - hd) * dx);
			normal.Normalize();

			const size_t i = static_cast<size_t>(z) * grid + x;
			vertices[i].normal = normal;

			Vec3 tangent = Vec3(dx, hr - hl, 0.f);
			tangent.Normalize();
			vertices[i].tangent = tangent;
		}
	}

	// --- 인덱스 ---
	vector<uint32> indices;
	indices.reserve(static_cast<size_t>(grid - 1) * (grid - 1) * 6);

	for (uint32 z = 0; z + 1 < grid; z++)
	{
		for (uint32 x = 0; x + 1 < grid; x++)
		{
			const uint32 i0 = z * grid + x;
			const uint32 i1 = z * grid + x + 1;
			const uint32 i2 = (z + 1) * grid + x;
			const uint32 i3 = (z + 1) * grid + x + 1;

			indices.push_back(i0); indices.push_back(i2); indices.push_back(i1);
			indices.push_back(i1); indices.push_back(i2); indices.push_back(i3);
		}
	}

	shared_ptr<Mesh> mesh = make_shared<Mesh>();
	mesh->Create(vertices, indices);
	GET_SINGLE(Resources)->Add<Mesh>(L"TerrainMesh", mesh);

	// --- 머티리얼 ---
	// 텍스처는 네 장까지다. 컬러맵 한 장 + 디테일 세 장으로 맞췄다.
	// 졸업작품은 여기에 스플랫맵과 각 층의 두 번째 변형까지 썼는데,
	// 그 스플랫맵(Test2.dds)이 색 블록 몇 개짜리 테스트 에셋이라 쓰지 않았다.
	// 대신 가중치를 경사와 높이에서 만든다.
	shared_ptr<Material> material =
		GET_SINGLE(Resources)->Get<Material>(L"Terrain")->Clone();

	Resources* resources = GET_SINGLE(Resources);
	material->SetTexture(0, resources->LoadTexture(L"..\\Resources\\Texture\\Terrain\\base.dds", true));
	material->SetTexture(1, resources->LoadTexture(L"..\\Resources\\Texture\\Terrain\\grass_detail.dds", true));
	material->SetTexture(2, resources->LoadTexture(L"..\\Resources\\Texture\\Terrain\\dirt_detail.dds", true));
	material->SetTexture(3, resources->LoadTexture(L"..\\Resources\\Texture\\Terrain\\rock_detail.dds", true));

	material->SetFloat(0, _desc.detailTiling);

	// 경사 판정 기준. 지형의 실제 기복에 맞춰 잡는다.
	material->SetVec4(0, Vec4(_desc.center.y, _desc.heightScale, 0.f, 0.f));

	shared_ptr<MeshRenderer> meshRenderer = GetGameObject()->GetMeshRenderer();
	if (meshRenderer == nullptr)
		return;

	meshRenderer->SetMesh(mesh);
	meshRenderer->SetMaterial(material);
}

float Terrain::GetHeight(float worldX, float worldZ) const
{
	const uint32 grid = _desc.gridSize;
	if (_heights.empty())
		return _desc.center.y;

	const float fx = (worldX - _origin.x) / _step;
	const float fz = (worldZ - _origin.z) / _step;

	if (fx < 0.f || fz < 0.f || fx > grid - 1.f || fz > grid - 1.f)
		return _desc.center.y;

	const uint32 x0 = static_cast<uint32>(fx);
	const uint32 z0 = static_cast<uint32>(fz);
	const uint32 x1 = (x0 + 1 < grid) ? x0 + 1 : x0;
	const uint32 z1 = (z0 + 1 < grid) ? z0 + 1 : z0;

	const float tx = fx - x0;
	const float tz = fz - z0;

	const float h00 = _heights[static_cast<size_t>(z0) * grid + x0];
	const float h10 = _heights[static_cast<size_t>(z0) * grid + x1];
	const float h01 = _heights[static_cast<size_t>(z1) * grid + x0];
	const float h11 = _heights[static_cast<size_t>(z1) * grid + x1];

	const float a = h00 + (h10 - h00) * tx;
	const float b = h01 + (h11 - h01) * tx;
	return a + (b - a) * tz;
}

Vec3 Terrain::GetNormal(float worldX, float worldZ) const
{
	// 한 칸 떨어진 높이 차이로 만든다. 메시 법선을 되찾는 것보다 짧고,
	// 나무를 세우는 용도로는 이 정밀도면 충분하다.
	const float d = _step;
	const float hl = GetHeight(worldX - d, worldZ);
	const float hr = GetHeight(worldX + d, worldZ);
	const float hd = GetHeight(worldX, worldZ - d);
	const float hu = GetHeight(worldX, worldZ + d);

	Vec3 normal = Vec3(-(hr - hl), 2.f * d, -(hu - hd));
	normal.Normalize();
	return normal;
}
