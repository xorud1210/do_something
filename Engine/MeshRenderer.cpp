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
	for (uint32 i = 0; i < _materials.size(); i++)
	{
		shared_ptr<Material>& material = _materials[i];

		if (material == nullptr || material->GetShader() == nullptr)
			continue;

		GetTransform()->PushData();

		if (GetAnimator())
		{
			GetAnimator()->PushData();
			material->SetInt(1, 1);
		}

		material->PushGraphicsData();
		_mesh->Render(1, i);
	}
}

void MeshRenderer::Render(shared_ptr<InstancingBuffer>& buffer)
{
	for (uint32 i = 0; i < _materials.size(); i++)
	{
		shared_ptr<Material>& material = _materials[i];

		if (material == nullptr || material->GetShader() == nullptr)
			continue;

		buffer->PushData();

		if (GetAnimator())
		{
			GetAnimator()->PushData();
			material->SetInt(1, 1);
		}

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