#include "pch.h"
#include "BillboardRenderer.h"
#include "StructuredBuffer.h"
#include "Material.h"
#include "Mesh.h"
#include "Texture.h"
#include "Resources.h"
#include "Transform.h"
#include "Timer.h"
#include "Engine.h"
#include "Camera.h"
#include "SceneManager.h"
#include "Scene.h"

#include <random>

// foliage_cull.fx 의 [numthreads] 와 같아야 한다.
constexpr uint32 FOLIAGE_CULL_GROUP_SIZE = 256;

ComPtr<ID3D12CommandSignature> BillboardRenderer::s_cmdSignature;
bool BillboardRenderer::s_cullEnabled = true;
bool BillboardRenderer::s_shadowEnabled = true;

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

	shared_ptr<Texture> texture = GET_SINGLE(Resources)->LoadColorTexture(
		_desc.textureKey, _desc.texturePath);
	_material->SetTexture(0, texture);

	_material->SetFloat(1, _desc.windStrength);
	_material->SetFloat(2, _desc.windFrequency);
	_material->SetFloat(3, _desc.heightRatio);

	Vec3 wind = _desc.windDirection;
	wind.Normalize();
	_material->SetVec4(0, Vec4(wind.x, wind.y, wind.z, 0.f));

	// 셰도우 패스용 머티리얼.
	// 셰이더만 다르고 바람/크기 상수는 화면과 완전히 같아야 한다.
	// 하나라도 어긋나면 그림자가 실제 풀과 다른 자리에 생긴다.
	if (_desc.castShadow)
	{
		_shadowMaterial = GET_SINGLE(Resources)->Get<Material>(L"BillboardShadow")->Clone();
		_shadowMaterial->SetTexture(0, texture);
		_shadowMaterial->SetFloat(1, _desc.windStrength);
		_shadowMaterial->SetFloat(2, _desc.windFrequency);
		_shadowMaterial->SetFloat(3, _desc.heightRatio);
		_shadowMaterial->SetVec4(0, Vec4(wind.x, wind.y, wind.z, 0.f));
	}

	CreateCullResources();
}

void BillboardRenderer::CreateCullResources()
{
	_cullMaterial = GET_SINGLE(Resources)->Get<Material>(L"ComputeFoliageCull")->Clone();

	// 통과한 것만 담을 곳. 최악의 경우 전부 통과하므로 크기는 같다.
	_visibleBuffer = make_shared<StructuredBuffer>();
	_visibleBuffer->Init(sizeof(BillboardInstance), _instanceCount);

	// 그리기 인자 버퍼.
	// uint 다섯 개짜리 구조적 버퍼로 만들어 두면 셰이더에서 g_args[1] 로
	// InstanceCount 필드를 직접 InterlockedAdd 할 수 있다.
	D3D12_DRAW_INDEXED_ARGUMENTS args = {};
	args.IndexCountPerInstance = _mesh->GetIndexCount();
	args.InstanceCount = 0;			// 컴퓨트 셰이더가 채운다
	args.StartIndexLocation = 0;
	args.BaseVertexLocation = 0;
	args.StartInstanceLocation = 0;

	constexpr uint32 argElementCount = sizeof(D3D12_DRAW_INDEXED_ARGUMENTS) / sizeof(uint32);
	static_assert(argElementCount == 5, "DRAW_INDEXED_ARGUMENTS layout changed");

	_argsBuffer = make_shared<StructuredBuffer>();
	_argsBuffer->Init(sizeof(uint32), argElementCount, &args);

	// 매 프레임 InstanceCount 를 0 으로 되돌려야 한다.
	// UPLOAD 힙은 UAV 가 될 수 없어 셰이더가 직접 못 만들고, 한 스레드에게
	// 0 을 쓰게 하면 같은 디스패치의 InterlockedAdd 와 경합한다.
	// 작은 원본에서 복사해 오는 쪽이 확실하다.
	{
		D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(args));
		D3D12_HEAP_PROPERTIES heap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);

		DEVICE->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
			D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&_argsReset));

		void* mapped = nullptr;
		D3D12_RANGE readRange{ 0, 0 };
		_argsReset->Map(0, &readRange, &mapped);
		::memcpy(mapped, &args, sizeof(args));
		_argsReset->Unmap(0, nullptr);
	}

	// 몇 장이 통과했는지 눈으로 확인하기 위한 되읽기 버퍼.
	// 컬링이 "작동은 하는데 아무것도 안 거른" 상태와 정상 상태는
	// 최종 화면이 똑같아서 구분할 수가 없다.
	{
		D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(args));
		D3D12_HEAP_PROPERTIES heap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);

		DEVICE->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
			D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&_argsReadback));

		_argsReadback->Map(0, nullptr, reinterpret_cast<void**>(&_readback));
	}

	// 커맨드 시그니처. 인자 버퍼의 해석 방법만 담는다.
	if (s_cmdSignature == nullptr)
	{
		D3D12_INDIRECT_ARGUMENT_DESC argDesc = {};
		argDesc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;

		D3D12_COMMAND_SIGNATURE_DESC sigDesc = {};
		sigDesc.ByteStride = sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);
		sigDesc.NumArgumentDescs = 1;
		sigDesc.pArgumentDescs = &argDesc;
		sigDesc.NodeMask = 0;

		// 루트 인자를 바꾸지 않는 시그니처라 루트 시그니처를 넘길 필요가 없다.
		DEVICE->CreateCommandSignature(&sigDesc, nullptr, IID_PPV_ARGS(&s_cmdSignature));
	}
}

void BillboardRenderer::FinalUpdate()
{
	_accTime += DELTA_TIME;
}

void BillboardRenderer::CullOnGPU()
{
	ID3D12Resource* args = _argsBuffer->GetBuffer().Get();
	constexpr uint64 argsSize = sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);

	// 1) InstanceCount 를 0 으로.
	{
		D3D12_RESOURCE_BARRIER toCopy = CD3DX12_RESOURCE_BARRIER::Transition(args,
			D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
		COMPUTE_CMD_LIST->ResourceBarrier(1, &toCopy);

		COMPUTE_CMD_LIST->CopyBufferRegion(args, 0, _argsReset.Get(), 0, argsSize);

		D3D12_RESOURCE_BARRIER toUav = CD3DX12_RESOURCE_BARRIER::Transition(args,
			D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		COMPUTE_CMD_LIST->ResourceBarrier(1, &toUav);
	}

	// 2) 절두체 판정.
	_instanceBuffer->PushComputeSRVData(SRV_REGISTER::t9);
	_visibleBuffer->PushComputeUAVData(UAV_REGISTER::u0);
	_argsBuffer->PushComputeUAVData(UAV_REGISTER::u1);

	// 카메라가 매 프레임 바뀌므로 뷰-투영 행렬을 그대로 넘긴다.
	// 평면 6장은 셰이더가 이 행렬에서 직접 뽑는다.
	Matrix matVP = Camera::S_MatView * Camera::S_MatProjection;
	_cullMaterial->SetMatrix(0, matVP);

	Matrix matViewInv = Camera::S_MatView.Invert();
	_cullMaterial->SetVec4(0, Vec4(matViewInv._41, matViewInv._42, matViewInv._43, 0.f));

	_cullMaterial->SetInt(0, static_cast<int32>(_instanceCount));
	_cullMaterial->SetFloat(0, _desc.heightRatio);
	_cullMaterial->SetFloat(1, _desc.windStrength);
	_cullMaterial->SetFloat(2, _desc.maxDrawDistance);

	_cullMaterial->PushComputeData();
	GEngine->GetComputeDescHeap()->CommitTable();

	const uint32 groupCount =
		(_instanceCount + FOLIAGE_CULL_GROUP_SIZE - 1) / FOLIAGE_CULL_GROUP_SIZE;
	COMPUTE_CMD_LIST->Dispatch(groupCount, 1, 1);

	// 3) 통과 개수를 CPU 로 되읽는다.
	//    Material::Dispatch 를 쓰지 않고 직접 기록하는 이유가 이것이다.
	//    바로 아래에서 컴퓨트 큐를 통째로 비우므로, 이 복사를 같은 커맨드 리스트에
	//    실어 두면 한 프레임 늦지 않고 이번 프레임 값이 그대로 온다.
	{
		D3D12_RESOURCE_BARRIER toSrc = CD3DX12_RESOURCE_BARRIER::Transition(args,
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
		COMPUTE_CMD_LIST->ResourceBarrier(1, &toSrc);

		COMPUTE_CMD_LIST->CopyBufferRegion(_argsReadback.Get(), 0, args, 0, argsSize);

		D3D12_RESOURCE_BARRIER toCommon = CD3DX12_RESOURCE_BARRIER::Transition(args,
			D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON);
		COMPUTE_CMD_LIST->ResourceBarrier(1, &toCommon);
	}

	GEngine->GetComputeCmdQueue()->FlushComputeCommandQueue();

	if (_readback)
		_visibleCount = _readback[1];
}

void BillboardRenderer::RenderShadow(uint32 cascade)
{
	if (_shadowMaterial == nullptr || _instanceCount == 0)
		return;

	// 설정한 구간까지만 그린다.
	if (cascade >= _desc.shadowCascadeCount)
		return;

	GetTransform()->PushData();

	// 셰도우 패스는 원본을 통째로 그린다.
	//
	// 화면용으로 추린 목록을 재활용하면 안 된다. 카메라 절두체 밖에 있는 풀도
	// 광원 방향에 따라 화면 안으로 그림자를 드리울 수 있기 때문이다.
	// 그 목록을 쓰면 화면 가장자리에서 그림자가 잘려 나간다.
	_instanceBuffer->PushGraphicsData(SRV_REGISTER::t9);

	// 사각형을 어느 쪽으로 돌릴지는 '메인 카메라' 가 정한다.
	// 이 패스에서 g_matViewInv 는 광원의 것이라 셰이더가 자기 힘으로 알 수 없다.
	// 광원 쪽으로 돌려버리면 그림자 모양이 화면의 풀과 달라진다.
	Vec3 camWorld = Vec3(0.f, 0.f, 0.f);
	if (shared_ptr<Scene> scene = GET_SINGLE(SceneManager)->GetActiveScene())
	{
		if (shared_ptr<Camera> mainCamera = scene->GetMainCamera())
		{
			Matrix inv = mainCamera->GetViewMatrix().Invert();
			camWorld = Vec3(inv._41, inv._42, inv._43);
		}
	}

	_shadowMaterial->SetFloat(0, _accTime);
	_shadowMaterial->SetVec4(1, Vec4(camWorld.x, camWorld.y, camWorld.z, 0.f));
	_shadowMaterial->PushGraphicsData();

	_mesh->Render(_instanceCount);
}

void BillboardRenderer::Render()
{
	if (_instanceBuffer == nullptr || _instanceCount == 0)
		return;

	const bool cull = s_cullEnabled && _cullMaterial != nullptr;

	if (cull)
		CullOnGPU();
	else
		_visibleCount = _instanceCount;

	GetTransform()->PushData();

	// 정점 셰이더가 인스턴스 ID 로 당겨올 목록.
	// 컬링을 켜면 추려진 목록, 끄면 원본을 그대로 본다.
	if (cull)
		_visibleBuffer->PushGraphicsData(SRV_REGISTER::t9);
	else
		_instanceBuffer->PushGraphicsData(SRV_REGISTER::t9);

	_material->SetFloat(0, _accTime);
	_material->PushGraphicsData();

	GEngine->SetStatusText(L"Foliage " + std::to_wstring(_visibleCount)
		+ L" / " + std::to_wstring(_instanceCount)
		+ (cull ? L"" : L" (cull off)")
		+ (IsCastShadow() ? L"" : L" (shadow off)"));

	if (cull == false)
	{
		_mesh->Render(_instanceCount);
		return;
	}

	// 인스턴스 개수는 CPU 가 모른다. 커맨드 프로세서가 버퍼에서 읽어 간다.
	ID3D12Resource* args = _argsBuffer->GetBuffer().Get();

	D3D12_RESOURCE_BARRIER toIndirect = CD3DX12_RESOURCE_BARRIER::Transition(args,
		D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
	GRAPHICS_CMD_LIST->ResourceBarrier(1, &toIndirect);

	_mesh->RenderIndirect(s_cmdSignature.Get(), args);

	D3D12_RESOURCE_BARRIER toCommon = CD3DX12_RESOURCE_BARRIER::Transition(args,
		D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, D3D12_RESOURCE_STATE_COMMON);
	GRAPHICS_CMD_LIST->ResourceBarrier(1, &toCommon);
}
