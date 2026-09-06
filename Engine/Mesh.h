#pragma once
#include "Object.h"

class Material;
class StructuredBuffer;

struct IndexBufferInfo
{
	ComPtr<ID3D12Resource>		buffer;
	D3D12_INDEX_BUFFER_VIEW		bufferView;
	DXGI_FORMAT					format;
	uint32						count;
};

struct KeyFrameInfo
{
	double	time;
	int32	frame;
	Vec3	scale;
	Vec4	rotation;
	Vec3	translate;
};

struct BoneInfo
{
	wstring					boneName;
	int32					parentIdx;
	Matrix					matOffset;
};

struct AnimClipInfo
{
	wstring			animName;
	int32			frameCount;
	double			duration;
	vector<vector<KeyFrameInfo>>	keyFrames;
};

class Mesh : public Object
{
public:
	Mesh();
	virtual ~Mesh();

	void Create(const vector<Vertex>& vertexBuffer, const vector<uint32>& indexbuffer);
	void Render(uint32 instanceCount = 1, uint32 idx = 0);

	// 인스턴스 개수를 CPU 가 모르는 채로 그린다.
	// 개수는 GPU 가 argBuffer 에 써 둔 값을 커맨드 프로세서가 읽어 간다.
	void RenderIndirect(ID3D12CommandSignature* signature, ID3D12Resource* argBuffer, uint32 idx = 0);
	void Render(shared_ptr<class InstancingBuffer>& buffer, uint32 idx = 0);

	static shared_ptr<Mesh> CreateFromFBX(const struct FbxMeshInfo* meshInfo, class FBXLoader& loader);
	static shared_ptr<Mesh> CreateFromBin(const struct BinMeshInfo* meshInfo, class BinLoader& loader);

private:
	void CreateVertexBuffer(const vector<Vertex>& buffer);
	void CreateIndexBuffer(const vector<uint32>& buffer);
	void CreateBounds(const vector<Vertex>& buffer);
	void CreateBonesAndAnimations(class FBXLoader& loader);
	void CreateBonesAndAnimationsFromBin(class BinLoader& loader);
	void CreateSkinBuffers();		// builds GPU buffers once _bones/_animClips are filled
	Matrix GetMatrix(FbxAMatrix& matrix);

public:
	uint32 GetSubsetCount() { return static_cast<uint32>(_vecIndexInfo.size()); }
	uint32 GetIndexCount(uint32 idx = 0) { return _vecIndexInfo[idx].count; }

	// 로컬 공간 바운딩 구. 정점에서 직접 잰다.
	// 스키닝 메시는 바인드 포즈 기준이라, 애니메이션이 이 밖으로 나갈 수 있다.
	// 그래서 컬링 쪽에서 여유를 더 준다.
	const Vec3& GetBoundsCenter() const { return _boundsCenter; }
	float GetBoundsRadius() const { return _boundsRadius; }
	const vector<BoneInfo>* GetBones() { return &_bones; }
	uint32						GetBoneCount() { return static_cast<uint32>(_bones.size()); }
	const vector<AnimClipInfo>* GetAnimClip() { return &_animClips; }

	bool							IsAnimMesh() { return !_animClips.empty(); }
	// All clips live in one buffer; a clip is addressed by its element offset.
	// Crossfading needs two clips at once, so they cannot be separate buffers.
	shared_ptr<StructuredBuffer>	GetBoneFrameDataBuffer() { return _frameBuffer; }
	int32							GetClipFrameOffset(int32 clipIndex)
	{
		if (clipIndex < 0 || clipIndex >= static_cast<int32>(_clipFrameOffset.size()))
			return 0;
		return _clipFrameOffset[clipIndex];
	} // 전체 본 프레임 정보
	shared_ptr<StructuredBuffer>	GetBoneOffsetBuffer() { return  _offsetBuffer; }

	// Bone frames may be parent-relative (.bin) or already baked to model space (FBX).
	// When local, the compute shader has to rebuild the hierarchy.
	shared_ptr<StructuredBuffer>	GetBoneParentBuffer() { return _boneParentBuffer; }
	bool							AreBoneFramesLocal() { return _boneFramesLocal; }
	void							SetBoneFramesLocal(bool value) { _boneFramesLocal = value; }

private:
	ComPtr<ID3D12Resource>		_vertexBuffer;
	D3D12_VERTEX_BUFFER_VIEW	_vertexBufferView = {};
	uint32 _vertexCount = 0;

	vector<IndexBufferInfo>		_vecIndexInfo;

	Vec3						_boundsCenter = Vec3(0.f, 0.f, 0.f);
	float						_boundsRadius = 0.f;

	// Animation
	vector<AnimClipInfo>			_animClips;
	vector<BoneInfo>				_bones;

	shared_ptr<StructuredBuffer>	_boneParentBuffer;	// int32 parent index per bone
	bool							_boneFramesLocal = false;
	shared_ptr<StructuredBuffer>	_offsetBuffer; // 각 뼈의 offset 행렬
	shared_ptr<StructuredBuffer>	_frameBuffer;		// every clip, concatenated
	vector<int32>					_clipFrameOffset;	// element offset of each clip // 전체 본 프레임 정보
};

