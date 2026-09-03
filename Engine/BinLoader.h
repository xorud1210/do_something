#pragma once

// ---------------------------------------------------------------------------
// BinLoader
//
// 졸업작품(LabProject)에서 쓰던 커스텀 계층 모델 포맷(.bin)을 읽어들인다.
// FBXLoader 와 같은 자리, 같은 역할이며 결과 구조체도 최대한 같은 모양으로 맞췄다.
// (Mesh::CreateFromBin / MeshData::LoadFromBin 이 FBX 경로를 그대로 흉내낼 수 있도록)
//
// 포맷 개요 — 문자열은 전부 "1바이트 길이 + 문자(널 종료 아님)"
//
//   <Hierarchy>:
//     <Frame>: {int idx} {int nTextures} {string name}
//       <Transform>:        float[13]           // pos3 + euler3 + scale3 + quat4, 안 씀
//       <TransformMatrix>:  float[16]           // 부모 기준 로컬 행렬
//       <Mesh>:             ...                 // 스태틱 메시
//       <SkinningInfo>:     ... 뒤이어 <Mesh>:   // 스키닝 메시
//       <Materials>:        {int n} ...
//       <Children>:         {int n} -> 재귀
//     </Frame>
//   </Hierarchy>
//   <Animation>:
//     <AnimationSets>: {int nSets}
//       <FrameNames>: {int nBones} {string[nBones]}
//       <AnimationSet>: {int idx} {string name} {float length} {int fps} {int nKeyFrames}
//         <Transforms>: {int key} {float time} {XMFLOAT4X4[nBones]}   // 로컬 행렬
//     </AnimationSets>
//   </Animation>:
//
// 본 인덱스 공간이 두 개라는 점에 주의.
//   - <FrameNames>  : 애니메이션 기준 본 목록. 엔진의 BoneInfo 인덱스는 여기에 맞춘다.
//   - <BoneNames>   : 스키닝 메시별 본 목록(부분집합). 정점의 본 인덱스는 여기를 가리킨다.
// RemapSkinningIndices() 가 후자를 전자의 공간으로 옮긴다.
// ---------------------------------------------------------------------------

struct BinMaterialInfo
{
	wstring			name;

	Vec4			albedo = Vec4(1.f, 1.f, 1.f, 1.f);
	Vec4			emissive = Vec4(0.f, 0.f, 0.f, 1.f);
	Vec4			specular = Vec4(0.f, 0.f, 0.f, 1.f);

	float			glossiness = 0.f;
	float			smoothness = 0.f;
	float			metallic = 0.f;
	float			specularHighlight = 0.f;
	float			glossyReflection = 0.f;

	wstring			diffuseTexName;		// <AlbedoMap>
	wstring			specularTexName;	// <SpecularMap>
	wstring			normalTexName;		// <NormalMap>
};

// .bin 안의 메시 하나. FbxMeshInfo 와 같은 모양.
struct BinMeshInfo
{
	wstring							name;
	vector<Vertex>					vertices;
	vector<vector<uint32>>			indices;		// 서브메시별 인덱스
	vector<BinMaterialInfo>			materials;
	bool							hasAnimation = false;

	// 이 메시를 소유한 계층 노드. 스태틱 메시의 월드 배치에 쓴다.
	int32							frameIndex = -1;
};

// 계층 노드. 애니메이션이 있으면 그대로 본이 된다.
struct BinFrameInfo
{
	wstring			name;
	int32			parentIndex = -1;		// _frames 공간
	Matrix			matToParent;			// 바인드포즈에서의 로컬 행렬
};

// 엔진 BoneInfo 로 그대로 옮겨갈 형태. FbxBoneInfo 와 같은 모양이되 Matrix 를 쓴다.
struct BinBoneInfo
{
	wstring			boneName;
	int32			parentIndex = -1;		// _bones 공간. 루트는 -1
	Matrix			matOffset;				// 바인드포즈 역행렬. 스키닝 본이 아니면 단위행렬
	Matrix			matToParent;			// 애니메이션이 없는 프레임용 폴백
};

struct BinKeyFrameInfo
{
	double			time = 0.0;
	int32			frame = 0;
	Vec3			scale = Vec3(1.f, 1.f, 1.f);
	Vec4			rotation = Vec4(0.f, 0.f, 0.f, 1.f);	// 쿼터니언
	Vec3			translate = Vec3(0.f, 0.f, 0.f);
};

struct BinAnimClipInfo
{
	wstring							name;
	int32							frameCount = 0;
	int32							fps = 30;
	double							duration = 0.0;			// 초
	vector<vector<BinKeyFrameInfo>>	keyFrames;				// [본][프레임]
};

class BinLoader
{
public:
	BinLoader();
	~BinLoader();

public:
	void LoadBin(const wstring& path);

public:
	int32 GetMeshCount() { return static_cast<int32>(_meshes.size()); }
	const BinMeshInfo& GetMesh(int32 idx) { return _meshes[idx]; }

	vector<shared_ptr<BinBoneInfo>>& GetBones() { return _bones; }
	vector<shared_ptr<BinAnimClipInfo>>& GetAnimClip() { return _animClips; }

	bool HasAnimation() { return _animClips.empty() == false; }

	int32 GetFrameCount() { return static_cast<int32>(_frames.size()); }
	const BinFrameInfo& GetFrame(int32 idx) { return _frames[idx]; }

	// 계층을 거슬러 올라가며 누적한 바인드포즈 월드 행렬.
	// 애니메이션이 없는 스태틱 메시를 제자리에 놓을 때 쓴다.
	Matrix GetFrameWorldMatrix(int32 frameIdx);

private:
	// 스키닝 메시별 <BoneNames> / <BoneOffsets> 임시 보관소.
	struct SkinCache
	{
		int32				meshIndex = -1;
		vector<wstring>		boneNames;
		vector<Matrix>		boneOffsets;
	};

private:
	// --- 저수준 리더 ---
	string	ReadString();					// 1바이트 길이 + 문자열. EOF 면 빈 문자열
	int32	ReadInt();
	float	ReadFloat();
	bool	ReadBytes(void* dest, size_t size);

	// --- 파싱 ---
	// headerAlreadyRead: <Frame>: 토큰을 호출부가 이미 소비한 경우 true.
	// 반환값은 이 프레임의 _frames 인덱스.
	int32	ParseFrame(int32 parentIndex, bool headerAlreadyRead = false);
	void	ParseMesh(BinMeshInfo& outMesh);
	void	ParseSkinningInfo(SkinCache& outSkin,
				vector<Vec4>& outBoneIndices, vector<Vec4>& outBoneWeights);
	void	ParseMaterials(BinMeshInfo& outMesh);
	void	ParseAnimationSets();

	// --- 후처리 ---
	void	BuildBoneTable();			// <FrameNames> 순서로 _bones 구성 + 부모 해결
	void	RemapSkinningIndices();		// 정점 본 인덱스를 _bones 공간으로 + 오프셋 배치

	void	LoadTextureIfExists(const wstring& path);
	shared_ptr<class Texture> FindTexture(const wstring& path);
	void	CreateTextures();
	void	CreateMaterials();

	int32	FindFrameIndex(const wstring& name);

	// "@T_Foo" -> "<모델폴더>/Textures/T_Foo.dds". "null" 이면 빈 문자열.
	wstring	ResolveTexturePath(const wstring& rawName);

private:
	FILE*			_file = nullptr;
	wstring			_resourceDirectory;

	vector<BinFrameInfo>				_frames;
	vector<SkinCache>					_skinCaches;

	// <FrameNames> 원본 순서. 본 인덱스의 기준.
	vector<wstring>						_animBoneNames;

	vector<BinMeshInfo>					_meshes;
	vector<shared_ptr<BinBoneInfo>>		_bones;
	vector<shared_ptr<BinAnimClipInfo>>	_animClips;
};
