#pragma once
#include "Object.h"

class Mesh;
class Material;
class GameObject;

struct MeshRenderInfo
{
	shared_ptr<Mesh>				mesh;
	vector<shared_ptr<Material>>	materials;

	// .bin 은 메시가 계층 노드에 붙어 있어서 노드의 누적 행렬만큼 옮겨야 제자리에 놓인다.
	// FBX 경로는 항상 단위행렬이라 동작이 달라지지 않는다.
	Matrix							matLocal = Matrix::Identity;
};

class MeshData : public Object
{
public:
	MeshData();
	virtual ~MeshData();

public:
	static shared_ptr<MeshData> LoadFromFBX(const wstring& path);
	static shared_ptr<MeshData> LoadFromBin(const wstring& path);

	virtual void Load(const wstring& path);
	virtual void Save(const wstring& path);

	vector<shared_ptr<GameObject>> Instantiate();

private:
	shared_ptr<Mesh>				_mesh;
	vector<shared_ptr<Material>>	_materials;

	vector<MeshRenderInfo> _meshRenders;
};
