#include "pch.h"
#include "MeshData.h"
#include "FBXLoader.h"
#include "BinLoader.h"
#include "Mesh.h"
#include "Material.h"
#include "Resources.h"
#include "Transform.h"
#include "MeshRenderer.h"

MeshData::MeshData() : Object(OBJECT_TYPE::MESH_DATA)
{
}

MeshData::~MeshData()
{
}

shared_ptr<MeshData> MeshData::LoadFromFBX(const wstring& path)
{
	FBXLoader loader;
	loader.LoadFbx(path);

	shared_ptr<MeshData> meshData = make_shared<MeshData>();

	for (int32 i = 0; i < loader.GetMeshCount(); i++)
	{
		shared_ptr<Mesh> mesh = Mesh::CreateFromFBX(&loader.GetMesh(i), loader);

		GET_SINGLE(Resources)->Add<Mesh>(mesh->GetName(), mesh);

		// Material 찾아서 연동
		vector<shared_ptr<Material>> materials;
		for (size_t j = 0; j < loader.GetMesh(i).materials.size(); j++)
		{
			shared_ptr<Material> material = GET_SINGLE(Resources)->Get<Material>(loader.GetMesh(i).materials[j].name);
			materials.push_back(material);
		}

		MeshRenderInfo info = {};
		info.mesh = mesh;
		info.materials = materials;
		meshData->_meshRenders.push_back(info);
	}

	return meshData;
}

void MeshData::Load(const wstring& _strFilePath)
{
	// TODO
}

void MeshData::Save(const wstring& _strFilePath)
{
	// TODO
}

vector<shared_ptr<GameObject>> MeshData::Instantiate()
{
	vector<shared_ptr<GameObject>> v;

	for (MeshRenderInfo& info : _meshRenders)
	{
		shared_ptr<GameObject> gameObject = make_shared<GameObject>();
		gameObject->AddComponent(make_shared<Transform>());
		gameObject->AddComponent(make_shared<MeshRenderer>());
		gameObject->GetMeshRenderer()->SetMesh(info.mesh);

		// Bake the accumulated frame matrix into the Transform.
		if (info.matLocal != Matrix::Identity)
		{
			Vec3 scale, translation;
			DirectX::SimpleMath::Quaternion rotation;
			if (info.matLocal.Decompose(scale, rotation, translation))
			{
				shared_ptr<Transform> transform = gameObject->GetTransform();
				transform->SetLocalScale(scale);
				transform->SetLocalPosition(translation);
				transform->SetLocalRotation(
					Transform::DecomposeRotationMatrix(Matrix::CreateFromQuaternion(rotation)));
			}
		}

		for (uint32 i = 0; i < info.materials.size(); i++)
			gameObject->GetMeshRenderer()->SetMaterial(info.materials[i], i);

		v.push_back(gameObject);
	}


	return v;
}


shared_ptr<MeshData> MeshData::LoadFromBin(const wstring& path)
{
	BinLoader loader;
	loader.LoadBin(path);

	shared_ptr<MeshData> meshData = make_shared<MeshData>();

	for (int32 i = 0; i < loader.GetMeshCount(); i++)
	{
		const BinMeshInfo& meshInfo = loader.GetMesh(i);

		shared_ptr<Mesh> mesh = Mesh::CreateFromBin(&meshInfo, loader);
		GET_SINGLE(Resources)->Add<Mesh>(mesh->GetName(), mesh);

		vector<shared_ptr<Material>> materials;
		for (size_t j = 0; j < meshInfo.materials.size(); j++)
		{
			shared_ptr<Material> material =
				GET_SINGLE(Resources)->Get<Material>(meshInfo.materials[j].name);
			materials.push_back(material);
		}

		MeshRenderInfo info = {};
		info.mesh = mesh;
		info.materials = materials;

		// Skinned meshes are placed by their bones, so the frame matrix must not be applied.
		// Only static meshes get positioned by the accumulated frame matrix.
		if (meshInfo.hasAnimation == false && meshInfo.frameIndex >= 0)
			info.matLocal = loader.GetFrameWorldMatrix(meshInfo.frameIndex);

		meshData->_meshRenders.push_back(info);
	}

	return meshData;
}
