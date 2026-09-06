#include "pch.h"
#include "MeshRenderer.h"
#include "Mesh.h"
#include "Material.h"
#include "Transform.h"
#include "InstancingBuffer.h"
#include "Resources.h"
#include "Animator.h"

MeshRenderer::MeshRenderer() : Component(COMPONENT_TYPE::MESH_RENDERER)
{

}

MeshRenderer::~MeshRenderer()
{

}

void MeshRenderer::SetMaterial(shared_ptr<Material> material, uint32 idx)
{
	if (_materials.size() <= static_cast<size_t>(idx))
		_materials.resize(static_cast<size_t>(idx + 1));

	_materials[idx] = material;
}

void MeshRenderer::Render()
{
	// 본 행렬 계산(컴퓨트 디스패치)은 오브젝트당 한 번이면 된다.
	// 예전에는 이 호출이 아래 루프 안에 있어서 서브셋 수만큼 같은 계산을 반복했다.
	if (GetAnimator())
		GetAnimator()->PushData();

	for (uint32 i = 0; i < _materials.size(); i++)
	{
		shared_ptr<Material>& material = _materials[i];

		if (material == nullptr || material->GetShader() == nullptr)
			continue;

		GetTransform()->PushData();

		// 그래픽스 디스크립터 그룹은 드로우마다 새로 잡히므로,
		// 계산은 한 번이어도 t7 묶기는 서브셋마다 다시 해야 한다.
		//
		// 그리고 플래그는 끄는 것까지 해야 한다. 머티리얼이 애니메이션 오브젝트와
		// 정적 오브젝트에 공유되면, 켜기만 하고 안 끄면 정적 쪽이 스키닝을 탄다.
		const bool skinned = GetAnimator() ? GetAnimator()->PushBoneData() : false;
		material->SetInt(1, skinned ? 1 : 0);

		// 인스턴싱 여부. 켜면 셰이더가 상수 버퍼 대신 정점 버퍼의 행렬을 쓴다.
		// 아무도 이 값을 쓰지 않고 있었다 - 같은 메시·머티리얼 오브젝트가
		// 둘 이상인 경우가 씬에 없어서 아래 인스턴싱 경로가 아예 안 돌았다.
		material->SetInt(0, 0);

		material->PushGraphicsData();
		_mesh->Render(1, i);
	}
}

void MeshRenderer::Render(shared_ptr<InstancingBuffer>& buffer)
{
	if (GetAnimator())
		GetAnimator()->PushData();

	for (uint32 i = 0; i < _materials.size(); i++)
	{
		shared_ptr<Material>& material = _materials[i];

		if (material == nullptr || material->GetShader() == nullptr)
			continue;

		buffer->PushData();

		const bool skinned = GetAnimator() ? GetAnimator()->PushBoneData() : false;
		material->SetInt(1, skinned ? 1 : 0);

		// 여기서는 트랜스폼 상수 버퍼를 밀지 않는다. 행렬이 정점 버퍼로 들어온다.
		material->SetInt(0, 1);

		material->PushGraphicsData();
		_mesh->Render(buffer, i);
	}
}

void MeshRenderer::RenderShadow()
{
	GetTransform()->PushData();

	shared_ptr<Material> material = GET_SINGLE(Resources)->Get<Material>(L"Shadow");

	// 셰도우 패스도 스키닝을 해야 한다.
	// 안 하면 애니메이션 중인 캐릭터가 바인드 포즈(T 포즈) 실루엣으로
	// 그림자를 드리운다. 화면의 캐릭터와 그림자가 서로 다른 자세가 된다.
	//
	// Shadow 머티리얼은 모든 오브젝트가 공유하므로 플래그를 오브젝트마다
	// 다시 써야 한다. 안 그러면 앞 오브젝트의 설정이 그대로 남는다.
	bool skinned = false;
	if (GetAnimator())
		skinned = GetAnimator()->PushBoneData();

	material->SetInt(1, skinned ? 1 : 0);
	material->PushGraphicsData();

	_mesh->Render();
}

uint64 MeshRenderer::GetInstanceID()
{
	if (_mesh == nullptr || _materials.empty())
		return 0;

	//uint64 id = (_mesh->GetID() << 32) | _material->GetID();
	InstanceID instanceID{ _mesh->GetID(), _materials[0]->GetID() };
	return instanceID.id;
}