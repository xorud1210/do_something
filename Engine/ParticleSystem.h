#pragma once
#include "Component.h"

class Material;
class Mesh;
class Texture;
class StructuredBuffer;

// 컴퓨트 셰이더가 스레드 그룹 하나(1024 스레드)로 돈다.
// 살릴 개수를 그룹 공유 카운터로 나눠 갖는 구조라 그룹이 늘어나면 그 카운터가 깨진다.
// 더 필요하면 이미터를 여러 개 두는 쪽이 맞다.
enum { PARTICLE_MAX_COUNT = 1024 };

enum class PARTICLE_EMITTER_SHAPE : uint8
{
	POINT,		// 한 점에서
	SPHERE,		// 구 안쪽에 고르게
	BOX,		// 정육면체 안쪽에
	CONE,		// 바닥 원에서 태우고 위쪽 콘 안으로 날린다
};

// 파티클 하나. 컴퓨트 셰이더의 Particle 구조체와 배치가 같아야 한다.
struct ParticleInfo
{
	Vec3	worldPos;
	float	curTime;
	Vec3	worldDir;		// 방향이 아니라 속도. 중력과 감속이 여기 누적된다
	float	lifeTime;
	int32	alive;
	float	rotation;
	float	rotationSpeed;
	float	seed;
};

struct ComputeSharedInfo
{
	int32 addCount;
	int32 padding[3];
};

// 이미터 설정. 전에는 전부 private 상수라 클래스를 고쳐야 값을 바꿀 수 있었다.
struct ParticleDesc
{
	uint32	maxParticle = 512;
	float	createInterval = 0.01f;		// 이 간격마다 하나씩 태운다

	float	minLifeTime = 0.5f;
	float	maxLifeTime = 1.0f;
	float	minSpeed = 50.f;
	float	maxSpeed = 100.f;

	float	startScale = 10.f;
	float	endScale = 5.f;

	// 수명에 따라 이 둘 사이를 오간다. 알파를 0으로 끝내야 사라질 때 끊기지 않는다.
	Vec4	startColor = Vec4(1.f, 1.f, 1.f, 1.f);
	Vec4	endColor = Vec4(1.f, 1.f, 1.f, 0.f);

	// 1보다 크면 HDR 범위로 올라가 블룸이 문다. 발광체는 이걸로 만든다.
	float	emissive = 1.f;

	PARTICLE_EMITTER_SHAPE	shape = PARTICLE_EMITTER_SHAPE::SPHERE;
	float	radius = 25.f;
	float	coneAngle = 0.5f;			// 콘 반각 (라디안)

	Vec3	gravity = Vec3(0.f, 0.f, 0.f);
	float	drag = 0.f;					// 초당 감속 비율
	float	maxSpin = 0.f;				// 최대 회전 속도 (라디안/초)

	// 파티클 사각형이 지면을 뚫을 때 생기는 교차선을 없앤다.
	// 이 거리 안쪽에서 서서히 투명해진다. 0이면 끈다.
	float	softFadeDistance = 0.f;

	bool	additive = false;			// 불꽃, 빛 같은 발광체는 가산이 맞다

	wstring	textureKey = L"ParticleGlow";
	wstring	texturePath = L"..\\Resources\\Texture\\Particle\\glow.png";
};

class ParticleSystem : public Component
{
public:
	ParticleSystem();
	virtual ~ParticleSystem();

public:
	virtual void FinalUpdate() override;
	void Render();

	// 버퍼와 머티리얼을 이 설정에 맞춰 다시 잡는다.
	void SetDesc(const ParticleDesc& desc);
	const ParticleDesc& GetDesc() const { return _desc; }

public:
	virtual void Load(const wstring& path) override { }
	virtual void Save(const wstring& path) override { }

private:
	void CreateBuffers();
	void CreateMaterials();

private:
	ParticleDesc					_desc;

	shared_ptr<StructuredBuffer>	_particleBuffer;
	shared_ptr<StructuredBuffer>	_computeSharedBuffer;
	uint32							_bufferCount = 0;

	// 인스턴스마다 복제해서 쓴다. 리소스의 원본을 그대로 쓰면
	// 이미터 두 개가 같은 머티리얼의 파라미터를 서로 덮어쓴다.
	shared_ptr<Material>			_material;
	shared_ptr<Material>			_computeMaterial;
	shared_ptr<Mesh>				_mesh;

	float							_accTime = 0.f;
};
