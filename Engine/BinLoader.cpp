#include "pch.h"
#include "BinLoader.h"
#include "Resources.h"
#include "Material.h"
#include "Texture.h"

#include <unordered_map>

BinLoader::BinLoader()
{
}

BinLoader::~BinLoader()
{
	if (_file)
	{
		::fclose(_file);
		_file = nullptr;
	}
}

// ---------------------------------------------------------------------------
// 저수준 리더
// ---------------------------------------------------------------------------

bool BinLoader::ReadBytes(void* dest, size_t size)
{
	return ::fread(dest, 1, size, _file) == size;
}

string BinLoader::ReadString()
{
	uint8 length = 0;
	if (::fread(&length, sizeof(uint8), 1, _file) != 1)
		return string();		// EOF

	if (length == 0)
		return string();

	char buffer[256] = {};
	if (length >= _countof(buffer))
		length = _countof(buffer) - 1;

	if (::fread(buffer, sizeof(char), length, _file) != length)
		return string();

	buffer[length] = '\0';
	return string(buffer);
}

int32 BinLoader::ReadInt()
{
	int32 value = 0;
	ReadBytes(&value, sizeof(int32));
	return value;
}

float BinLoader::ReadFloat()
{
	float value = 0.f;
	ReadBytes(&value, sizeof(float));
	return value;
}

// ---------------------------------------------------------------------------
// 진입점
// ---------------------------------------------------------------------------

void BinLoader::LoadBin(const wstring& path)
{
	_resourceDirectory = fs::path(path).parent_path().wstring() + L"/";

	::fopen_s(&_file, ws2s(path).c_str(), "rb");

	assert(_file != nullptr);
	if (_file == nullptr)
		return;

	for (;;)
	{
		string token = ReadString();
		if (token.empty())
			break;							// EOF

		if (token == "<Hierarchy>:")
		{
			ParseFrame(-1);
			ReadString();					// "</Hierarchy>"
		}
		else if (token == "<Frame>:")
		{
			// 스태틱 모델(Pine.bin 등)은 <Hierarchy>: 래퍼 없이 <Frame>: 으로 바로 시작한다.
			// 원본의 CStaticObject 도 LoadFrameHierarchyFromFile 을 곧바로 부른다.
			ParseFrame(-1, /*headerAlreadyRead=*/true);
		}
		else if (token == "<Animation>:")
		{
			ParseAnimationSets();
		}
		else if (token == "</Animation>" || token == "</Animation>:")
		{
			// 실제 파일에 들어있는 토큰은 콜론이 없는 "</Animation>" 이다.
			// (원본 로더는 "</Animation>:" 로 비교해서 늘 빗나가고 EOF 로만 빠져나왔다)
			break;
		}
	}

	::fclose(_file);
	_file = nullptr;

	// 애니메이션이 있는 모델만 본 테이블을 만든다.
	// 스태틱 모델은 _frames + BinMeshInfo::frameIndex 만으로 배치가 끝난다.
	if (_animBoneNames.empty() == false)
	{
		BuildBoneTable();
		RemapSkinningIndices();
	}

	CreateTextures();
	CreateMaterials();

}

// ---------------------------------------------------------------------------
// 텍스처 / 머티리얼
// ---------------------------------------------------------------------------

wstring BinLoader::ResolveTexturePath(const wstring& rawName)
{
	if (rawName.empty() || rawName == L"null")
		return wstring();

	// '@' 는 익스포터가 붙인 "이미 쓴 텍스처" 표시다. 파일명에는 들어가지 않는다.
	wstring name = (rawName[0] == L'@') ? rawName.substr(1) : rawName;
	if (name.empty())
		return wstring();

	// 원본은 "Model/Textures/" 고정 경로에 .dds 를 붙여 썼다.
	return _resourceDirectory + L"Textures/" + name + L".dds";
}

void BinLoader::LoadTextureIfExists(const wstring& path, bool srgb)
{
	if (path.empty())
		return;
	if (fs::exists(path) == false)
		return;

	// 캐시 키는 Resources 가 정한다. 여기서 파일명만 떼어 쓰면
	// 다른 폴더의 같은 이름이 서로를 덮고, sRGB 해석도 섞인다.
	GET_SINGLE(Resources)->LoadTexture(path, srgb);
}

shared_ptr<Texture> BinLoader::FindTexture(const wstring& path, bool srgb)
{
	return GET_SINGLE(Resources)->FindTexture(path, srgb);
}

void BinLoader::CreateTextures()
{
	for (BinMeshInfo& mesh : _meshes)
	{
		for (BinMaterialInfo& material : mesh.materials)
		{
			// 디퓨즈만 색이다. 노멀맵은 방향 벡터, 스페큘러맵은 반사율 계수라
			// sRGB 곡선을 먹이면 값 자체가 틀어진다.
			LoadTextureIfExists(ResolveTexturePath(material.diffuseTexName), true);
			LoadTextureIfExists(ResolveTexturePath(material.normalTexName), false);
			LoadTextureIfExists(ResolveTexturePath(material.specularTexName), false);
		}
	}
}

void BinLoader::CreateMaterials()
{
	for (size_t i = 0; i < _meshes.size(); i++)
	{
		for (size_t j = 0; j < _meshes[i].materials.size(); j++)
		{
			BinMaterialInfo& info = _meshes[i].materials[j];

			// .bin 에는 머티리얼 이름이 없다. 메시 이름 + 인덱스로 고유 키를 만든다.
			if (info.name.empty())
				info.name = _meshes[i].name + L"_mat" + std::to_wstring(j);

			if (GET_SINGLE(Resources)->Get<Material>(info.name) != nullptr)
				continue;

			shared_ptr<Material> material = make_shared<Material>();
			material->SetName(info.name);
			material->SetShader(GET_SINGLE(Resources)->Get<Shader>(L"Deferred"));

			// 로드할 때와 같은 해석으로 찾아야 한다. 디퓨즈만 색이다.
			shared_ptr<Texture> diffuse = FindTexture(ResolveTexturePath(info.diffuseTexName), true);
			if (diffuse)
				material->SetTexture(0, diffuse);

			shared_ptr<Texture> normal = FindTexture(ResolveTexturePath(info.normalTexName), false);
			if (normal)
				material->SetTexture(1, normal);

			shared_ptr<Texture> specular = FindTexture(ResolveTexturePath(info.specularTexName), false);
			if (specular)
				material->SetTexture(2, specular);

			GET_SINGLE(Resources)->Add<Material>(material->GetName(), material);
		}
	}
}

// ---------------------------------------------------------------------------
// 계층
// ---------------------------------------------------------------------------

int32 BinLoader::ParseFrame(int32 parentIndex, bool headerAlreadyRead)
{
	// 자식으로 재귀하기 전에 자리를 먼저 잡아둬야 부모 인덱스가 어긋나지 않는다.
	const int32 myIndex = static_cast<int32>(_frames.size());
	_frames.emplace_back();
	_frames[myIndex].parentIndex = parentIndex;
	_frames[myIndex].matToParent = Matrix::Identity;

	if (headerAlreadyRead)
	{
		ReadInt();							// 프레임 인덱스(안 씀)
		ReadInt();							// 텍스처 개수(안 씀)
		_frames[myIndex].name = s2ws(ReadString());
	}

	// <SkinningInfo> 가 <Mesh> 보다 먼저 나오므로 정점 가중치를 잠시 들고 있는다.
	vector<Vec4> pendingBoneIndices;
	vector<Vec4> pendingBoneWeights;

	for (;;)
	{
		string token = ReadString();
		if (token.empty() || token == "</Frame>")
			break;

		if (token == "<Frame>:")
		{
			ReadInt();						// 프레임 인덱스(안 씀)
			ReadInt();						// 텍스처 개수(안 씀)
			_frames[myIndex].name = s2ws(ReadString());
		}
		else if (token == "<Transform>:")
		{
			// pos3 + euler3 + scale3 + quat4. <TransformMatrix> 를 쓰므로 건너뛴다.
			float discard[13] = {};
			ReadBytes(discard, sizeof(float) * 13);
		}
		else if (token == "<TransformMatrix>:")
		{
			XMFLOAT4X4 mat = {};
			ReadBytes(&mat, sizeof(float) * 16);
			_frames[myIndex].matToParent = Matrix(XMLoadFloat4x4(&mat));
		}
		else if (token == "<SkinningInfo>:")
		{
			SkinCache skin;
			ParseSkinningInfo(skin, pendingBoneIndices, pendingBoneWeights);

			// 스키닝 정보 뒤에는 반드시 <Mesh>: 가 따라온다.
			string next = ReadString();
			if (next == "<Mesh>:")
			{
				BinMeshInfo mesh;
				mesh.frameIndex = myIndex;
				mesh.hasAnimation = true;
				ParseMesh(mesh);

				// 정점 배열이 생겼으니 이제 가중치를 심는다.
				const size_t count = min(mesh.vertices.size(), pendingBoneIndices.size());
				for (size_t i = 0; i < count; i++)
				{
					mesh.vertices[i].indices = pendingBoneIndices[i];
					mesh.vertices[i].weights = pendingBoneWeights[i];
				}

				skin.meshIndex = static_cast<int32>(_meshes.size());
				_meshes.push_back(std::move(mesh));
				_skinCaches.push_back(std::move(skin));
			}
		}
		else if (token == "<Mesh>:")
		{
			BinMeshInfo mesh;
			mesh.frameIndex = myIndex;
			ParseMesh(mesh);
			_meshes.push_back(std::move(mesh));
		}
		else if (token == "<Materials>:")
		{
			// 머티리얼은 직전에 읽은 메시에 붙는다.
			if (_meshes.empty() == false)
				ParseMaterials(_meshes.back());
			else
			{
				BinMeshInfo dummy;			// 메시 없는 프레임의 머티리얼은 버린다
				ParseMaterials(dummy);
			}
		}
		else if (token == "<Children>:")
		{
			const int32 childCount = ReadInt();
			for (int32 i = 0; i < childCount; i++)
				ParseFrame(myIndex);
		}
	}

	return myIndex;
}

// ---------------------------------------------------------------------------
// 메시
// ---------------------------------------------------------------------------

void BinLoader::ParseMesh(BinMeshInfo& outMesh)
{
	const int32 vertexCount = ReadInt();
	outMesh.name = s2ws(ReadString());

	if (vertexCount > 0)
		outMesh.vertices.resize(vertexCount);

	for (;;)
	{
		string token = ReadString();
		if (token.empty() || token == "</Mesh>")
			break;

		if (token == "<Bounds>:")
		{
			float discard[6] = {};			// center3 + extents3
			ReadBytes(discard, sizeof(float) * 6);
		}
		else if (token == "<Positions>:")
		{
			const int32 count = ReadInt();
			if (count <= 0) continue;

			vector<XMFLOAT3> data(count);
			ReadBytes(data.data(), sizeof(XMFLOAT3) * count);

			if (outMesh.vertices.size() < static_cast<size_t>(count))
				outMesh.vertices.resize(count);
			for (int32 i = 0; i < count; i++)
				outMesh.vertices[i].pos = Vec3(data[i]);
		}
		else if (token == "<Colors>:")
		{
			const int32 count = ReadInt();
			if (count <= 0) continue;
			// 엔진 Vertex 에 컬러 슬롯이 없다. 읽고 버린다.
			vector<XMFLOAT4> data(count);
			ReadBytes(data.data(), sizeof(XMFLOAT4) * count);
		}
		else if (token == "<TextureCoords0>:")
		{
			const int32 count = ReadInt();
			if (count <= 0) continue;

			vector<XMFLOAT2> data(count);
			ReadBytes(data.data(), sizeof(XMFLOAT2) * count);

			const int32 n = min(count, static_cast<int32>(outMesh.vertices.size()));
			for (int32 i = 0; i < n; i++)
				outMesh.vertices[i].uv = Vec2(data[i]);
		}
		else if (token == "<TextureCoords1>:")
		{
			const int32 count = ReadInt();
			if (count <= 0) continue;
			// 두 번째 UV 셋은 아직 안 쓴다.
			vector<XMFLOAT2> data(count);
			ReadBytes(data.data(), sizeof(XMFLOAT2) * count);
		}
		else if (token == "<Normals>:")
		{
			const int32 count = ReadInt();
			if (count <= 0) continue;

			vector<XMFLOAT3> data(count);
			ReadBytes(data.data(), sizeof(XMFLOAT3) * count);

			const int32 n = min(count, static_cast<int32>(outMesh.vertices.size()));
			for (int32 i = 0; i < n; i++)
				outMesh.vertices[i].normal = Vec3(data[i]);
		}
		else if (token == "<Tangents>:")
		{
			const int32 count = ReadInt();
			if (count <= 0) continue;

			vector<XMFLOAT3> data(count);
			ReadBytes(data.data(), sizeof(XMFLOAT3) * count);

			const int32 n = min(count, static_cast<int32>(outMesh.vertices.size()));
			for (int32 i = 0; i < n; i++)
				outMesh.vertices[i].tangent = Vec3(data[i]);
		}
		else if (token == "<BiTangents>:")
		{
			const int32 count = ReadInt();
			if (count <= 0) continue;
			// 노멀 x 탄젠트로 셰이더에서 만들 수 있다. 읽고 버린다.
			vector<XMFLOAT3> data(count);
			ReadBytes(data.data(), sizeof(XMFLOAT3) * count);
		}
		else if (token == "<SubMeshes>:")
		{
			const int32 subMeshCount = ReadInt();
			for (int32 i = 0; i < subMeshCount; i++)
			{
				string subToken = ReadString();
				if (subToken != "<SubMesh>:")
					break;

				ReadInt();								// 서브메시 인덱스(안 씀)
				const int32 indexCount = ReadInt();

				vector<uint32> indices;
				if (indexCount > 0)
				{
					indices.resize(indexCount);
					ReadBytes(indices.data(), sizeof(uint32) * indexCount);
				}
				outMesh.indices.push_back(std::move(indices));
			}
		}
	}

	// 서브메시가 하나도 없으면 렌더할 수 없다. 빈 슬롯이라도 만들어 둔다.
	if (outMesh.indices.empty())
		outMesh.indices.emplace_back();
}

// ---------------------------------------------------------------------------
// 스키닝
// ---------------------------------------------------------------------------

void BinLoader::ParseSkinningInfo(SkinCache& outSkin,
	vector<Vec4>& outBoneIndices, vector<Vec4>& outBoneWeights)
{
	ReadString();		// 메시 이름(뒤따르는 <Mesh> 쪽 이름을 쓴다)

	for (;;)
	{
		string token = ReadString();
		if (token.empty() || token == "</SkinningInfo>")
			break;

		if (token == "<BonesPerVertex>:")
		{
			ReadInt();							// 항상 4로 가정
		}
		else if (token == "<Bounds>:")
		{
			float discard[6] = {};
			ReadBytes(discard, sizeof(float) * 6);
		}
		else if (token == "<BoneNames>:")
		{
			const int32 count = ReadInt();
			outSkin.boneNames.reserve(count);
			for (int32 i = 0; i < count; i++)
				outSkin.boneNames.push_back(s2ws(ReadString()));
		}
		else if (token == "<BoneOffsets>:")
		{
			const int32 count = ReadInt();
			if (count <= 0) continue;

			vector<XMFLOAT4X4> data(count);
			ReadBytes(data.data(), sizeof(XMFLOAT4X4) * count);

			outSkin.boneOffsets.reserve(count);
			for (int32 i = 0; i < count; i++)
				outSkin.boneOffsets.push_back(Matrix(XMLoadFloat4x4(&data[i])));
		}
		else if (token == "<BoneIndices>:")
		{
			const int32 count = ReadInt();
			if (count <= 0) continue;

			vector<XMINT4> data(count);
			ReadBytes(data.data(), sizeof(XMINT4) * count);

			outBoneIndices.resize(count);
			for (int32 i = 0; i < count; i++)
			{
				outBoneIndices[i] = Vec4(
					static_cast<float>(data[i].x), static_cast<float>(data[i].y),
					static_cast<float>(data[i].z), static_cast<float>(data[i].w));
			}
		}
		else if (token == "<BoneWeights>:")
		{
			const int32 count = ReadInt();
			if (count <= 0) continue;

			vector<XMFLOAT4> data(count);
			ReadBytes(data.data(), sizeof(XMFLOAT4) * count);

			outBoneWeights.resize(count);
			for (int32 i = 0; i < count; i++)
				outBoneWeights[i] = Vec4(data[i]);
		}
	}
}

// ---------------------------------------------------------------------------
// 머티리얼
// ---------------------------------------------------------------------------

void BinLoader::ParseMaterials(BinMeshInfo& outMesh)
{
	const int32 materialCount = ReadInt();
	if (materialCount <= 0)
		return;

	outMesh.materials.resize(materialCount);
	BinMaterialInfo* current = nullptr;

	for (;;)
	{
		string token = ReadString();
		if (token.empty() || token == "</Materials>")
			break;

		if (token == "<Material>:")
		{
			const int32 idx = ReadInt();
			current = (idx >= 0 && idx < materialCount) ? &outMesh.materials[idx] : nullptr;
			continue;
		}

		// <Material>: 앞에 오는 토큰은 없어야 하지만, 방어적으로 처리한다.
		if (current == nullptr)
		{
			// 알 수 없는 위치의 토큰은 건너뛸 크기를 알 수 없으므로 중단한다.
			break;
		}

		if (token == "<AlbedoColor>:")				ReadBytes(&current->albedo, sizeof(float) * 4);
		else if (token == "<EmissiveColor>:")		ReadBytes(&current->emissive, sizeof(float) * 4);
		else if (token == "<SpecularColor>:")		ReadBytes(&current->specular, sizeof(float) * 4);
		else if (token == "<Glossiness>:")			current->glossiness = ReadFloat();
		else if (token == "<Smoothness>:")			current->smoothness = ReadFloat();
		// 원본 로더가 Metallic <-> SpecularHighlight 를 서로 바꿔 담고 있다.
		// 익스포터가 쓴 순서를 그대로 따르므로 여기서도 같은 순서로 읽는다.
		else if (token == "<Metallic>:")			current->specularHighlight = ReadFloat();
		else if (token == "<SpecularHighlight>:")	current->metallic = ReadFloat();
		else if (token == "<GlossyReflection>:")		current->glossyReflection = ReadFloat();
		else if (token == "<AlbedoMap>:")			current->diffuseTexName = s2ws(ReadString());
		else if (token == "<SpecularMap>:")			current->specularTexName = s2ws(ReadString());
		else if (token == "<NormalMap>:")			current->normalTexName = s2ws(ReadString());
		else if (token == "<MetallicMap>:")			ReadString();
		else if (token == "<EmissionMap>:")			ReadString();
		else if (token == "<DetailAlbedoMap>:")		ReadString();
		else if (token == "<DetailNormalMap>:")		ReadString();
	}
}

// ---------------------------------------------------------------------------
// 애니메이션
// ---------------------------------------------------------------------------

void BinLoader::ParseAnimationSets()
{
	int32 animSetCount = 0;

	for (;;)
	{
		string token = ReadString();
		if (token.empty() || token == "</AnimationSets>")
			break;

		if (token == "<AnimationSets>:")
		{
			animSetCount = ReadInt();
			_animClips.resize(animSetCount);
		}
		else if (token == "<FrameNames>:")
		{
			const int32 boneCount = ReadInt();
			_animBoneNames.reserve(boneCount);
			for (int32 i = 0; i < boneCount; i++)
				_animBoneNames.push_back(s2ws(ReadString()));
		}
		else if (token == "<AnimationSet>:")
		{
			const int32 setIndex = ReadInt();
			const string  name = ReadString();
			const float   length = ReadFloat();
			const int32   fps = ReadInt();
			const int32   keyFrameCount = ReadInt();

			const int32 boneCount = static_cast<int32>(_animBoneNames.size());

			auto clip = make_shared<BinAnimClipInfo>();
			clip->name = s2ws(name);
			clip->frameCount = keyFrameCount;
			clip->fps = fps;
			clip->duration = static_cast<double>(length);
			clip->keyFrames.resize(boneCount);
			for (int32 b = 0; b < boneCount; b++)
				clip->keyFrames[b].resize(keyFrameCount);

			vector<XMFLOAT4X4> frameTransforms(boneCount);

			for (int32 k = 0; k < keyFrameCount; k++)
			{
				string transformToken = ReadString();
				if (transformToken != "<Transforms>:")
					break;

				ReadInt();								// 키 인덱스(안 씀)
				const float keyTime = ReadFloat();

				if (boneCount > 0)
					ReadBytes(frameTransforms.data(), sizeof(XMFLOAT4X4) * boneCount);

				// 행렬 -> SRT 분해. 엔진 애니메이션은 쿼터니언 slerp 로 보간한다.
				for (int32 b = 0; b < boneCount; b++)
				{
					XMVECTOR scale, rotation, translation;
					const XMMATRIX mat = XMLoadFloat4x4(&frameTransforms[b]);

					BinKeyFrameInfo& key = clip->keyFrames[b][k];
					key.time = static_cast<double>(keyTime);
					key.frame = k;

					if (XMMatrixDecompose(&scale, &rotation, &translation, mat))
					{
						key.scale = Vec3(scale);
						key.rotation = Vec4(rotation);
						key.translate = Vec3(translation);
					}
					else
					{
						// 분해 실패(뒤집힌 스케일 등). 이전 프레임 값을 물려받는다.
						if (k > 0)
							key = clip->keyFrames[b][k - 1];
						key.frame = k;
						key.time = static_cast<double>(keyTime);
					}
				}
			}

			if (setIndex >= 0 && setIndex < static_cast<int32>(_animClips.size()))
				_animClips[setIndex] = clip;
			else
				_animClips.push_back(clip);
		}
	}

	// 빈 슬롯이 남았으면 정리한다.
	_animClips.erase(
		std::remove(_animClips.begin(), _animClips.end(), nullptr),
		_animClips.end());
}

// ---------------------------------------------------------------------------
// 후처리
// ---------------------------------------------------------------------------

int32 BinLoader::FindFrameIndex(const wstring& name)
{
	// 이름이 중복되는 프레임이 있다(예: Player.bin 의 "Head").
	// 원본 로더의 FindFrame 과 동일하게 먼저 나온 것을 쓴다.
	for (int32 i = 0; i < static_cast<int32>(_frames.size()); i++)
	{
		if (_frames[i].name == name)
			return i;
	}
	return -1;
}

void BinLoader::BuildBoneTable()
{
	const int32 boneCount = static_cast<int32>(_animBoneNames.size());
	_bones.resize(boneCount);

	// 본 이름 -> 본 인덱스
	std::unordered_map<wstring, int32> boneNameToIndex;
	boneNameToIndex.reserve(boneCount);
	for (int32 i = 0; i < boneCount; i++)
		boneNameToIndex.emplace(_animBoneNames[i], i);

	for (int32 i = 0; i < boneCount; i++)
	{
		auto bone = make_shared<BinBoneInfo>();
		bone->boneName = _animBoneNames[i];
		bone->matOffset = Matrix::Identity;
		bone->matToParent = Matrix::Identity;
		bone->parentIndex = -1;

		const int32 frameIndex = FindFrameIndex(_animBoneNames[i]);
		if (frameIndex >= 0)
		{
			bone->matToParent = _frames[frameIndex].matToParent;

			// 계층상의 부모를 본 인덱스 공간으로 옮긴다.
			// 부모가 <FrameNames> 에 없으면 있을 때까지 더 거슬러 올라간다.
			int32 parentFrame = _frames[frameIndex].parentIndex;
			while (parentFrame >= 0)
			{
				auto findIt = boneNameToIndex.find(_frames[parentFrame].name);
				if (findIt != boneNameToIndex.end())
				{
					bone->parentIndex = findIt->second;
					break;
				}
				parentFrame = _frames[parentFrame].parentIndex;
			}
		}

		_bones[i] = bone;
	}
}

void BinLoader::RemapSkinningIndices()
{
	std::unordered_map<wstring, int32> boneNameToIndex;
	boneNameToIndex.reserve(_animBoneNames.size());
	for (int32 i = 0; i < static_cast<int32>(_animBoneNames.size()); i++)
		boneNameToIndex.emplace(_animBoneNames[i], i);

	for (const SkinCache& skin : _skinCaches)
	{
		if (skin.meshIndex < 0 || skin.meshIndex >= static_cast<int32>(_meshes.size()))
			continue;

		// 스키닝 본 인덱스 -> 전역 본 인덱스
		const int32 localCount = static_cast<int32>(skin.boneNames.size());
		vector<int32> localToGlobal(localCount, 0);

		for (int32 i = 0; i < localCount; i++)
		{
			auto findIt = boneNameToIndex.find(skin.boneNames[i]);
			localToGlobal[i] = (findIt != boneNameToIndex.end()) ? findIt->second : 0;

			// 바인드포즈 역행렬을 전역 본 배열에 흩뿌린다.
			if (findIt != boneNameToIndex.end() &&
				i < static_cast<int32>(skin.boneOffsets.size()))
			{
				_bones[findIt->second]->matOffset = skin.boneOffsets[i];
			}
		}

		// 정점이 들고 있는 인덱스를 전역 공간으로 바꾼다.
		auto remap = [&](float local) -> float
		{
			const int32 idx = static_cast<int32>(local);
			if (idx < 0 || idx >= localCount)
				return 0.f;
			return static_cast<float>(localToGlobal[idx]);
		};

		for (Vertex& vertex : _meshes[skin.meshIndex].vertices)
		{
			vertex.indices.x = remap(vertex.indices.x);
			vertex.indices.y = remap(vertex.indices.y);
			vertex.indices.z = remap(vertex.indices.z);
			vertex.indices.w = remap(vertex.indices.w);
		}
	}
}

Matrix BinLoader::GetFrameWorldMatrix(int32 frameIdx)
{
	Matrix world = Matrix::Identity;

	int32 cursor = frameIdx;
	while (cursor >= 0 && cursor < static_cast<int32>(_frames.size()))
	{
		world = world * _frames[cursor].matToParent;
		cursor = _frames[cursor].parentIndex;
	}

	return world;
}
