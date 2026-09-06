#include "pch.h"
#include "Mesh.h"
#include "Engine.h"
#include "Material.h"
#include "InstancingBuffer.h"
#include "FBXLoader.h"
#include "BinLoader.h"
#include "StructuredBuffer.h"

Mesh::Mesh() : Object(OBJECT_TYPE::MESH)
{

}

Mesh::~Mesh()
{

}

void Mesh::Create(const vector<Vertex>& vertexBuffer, const vector<uint32>& indexBuffer)
{
	CreateVertexBuffer(vertexBuffer);
	CreateIndexBuffer(indexBuffer);
	CreateBounds(vertexBuffer);
}

// 로컬 공간 바운딩 구.
//
// AABB 의 중심을 잡고 거기서 가장 먼 정점까지를 반지름으로 한다.
// 최소 구는 아니지만 한 번만 돌면 되고, 컬링에서 너무 크게 잡는 것은
// 안전한 방향의 오차다(안 보이는 걸 그릴 뿐, 보이는 걸 지우지 않는다).
void Mesh::CreateBounds(const vector<Vertex>& buffer)
{
	if (buffer.empty())
	{
		_boundsCenter = Vec3(0.f, 0.f, 0.f);
		_boundsRadius = 0.f;
		return;
	}

	Vec3 minPos = buffer[0].pos;
	Vec3 maxPos = buffer[0].pos;

	for (const Vertex& v : buffer)
	{
		minPos = Vec3::Min(minPos, v.pos);
		maxPos = Vec3::Max(maxPos, v.pos);
	}

	_boundsCenter = (minPos + maxPos) * 0.5f;

	float radiusSq = 0.f;
	for (const Vertex& v : buffer)
	{
		const float d = (v.pos - _boundsCenter).LengthSquared();
		if (d > radiusSq)
			radiusSq = d;
	}

	_boundsRadius = ::sqrtf(radiusSq);
}

void Mesh::Render(uint32 instanceCount, uint32 idx)
{
	GRAPHICS_CMD_LIST->IASetVertexBuffers(0, 1, &_vertexBufferView); // Slot: (0~15)
	GRAPHICS_CMD_LIST->IASetIndexBuffer(&_vecIndexInfo[idx].bufferView);

	GEngine->GetGraphicsDescHeap()->CommitTable();

	GRAPHICS_CMD_LIST->DrawIndexedInstanced(_vecIndexInfo[idx].count, instanceCount, 0, 0, 0);
}

void Mesh::RenderIndirect(ID3D12CommandSignature* signature, ID3D12Resource* argBuffer, uint32 idx)
{
	GRAPHICS_CMD_LIST->IASetVertexBuffers(0, 1, &_vertexBufferView);
	GRAPHICS_CMD_LIST->IASetIndexBuffer(&_vecIndexInfo[idx].bufferView);

	GEngine->GetGraphicsDescHeap()->CommitTable();

	// DrawIndexedInstanced 와 하는 일은 같다. 다른 점은 다섯 개의 인자를
	// CPU 가 넣지 않고 커맨드 프로세서가 버퍼에서 읽어 간다는 것뿐이다.
	// 그래서 인스턴스 개수를 컴퓨트 셰이더가 정할 수 있다.
	GRAPHICS_CMD_LIST->ExecuteIndirect(signature, 1, argBuffer, 0, nullptr, 0);
}

void Mesh::Render(shared_ptr<InstancingBuffer>& buffer, uint32 idx)
{
	D3D12_VERTEX_BUFFER_VIEW bufferViews[] = { _vertexBufferView, buffer->GetBufferView() };
	GRAPHICS_CMD_LIST->IASetVertexBuffers(0, 2, bufferViews);
	GRAPHICS_CMD_LIST->IASetIndexBuffer(&_vecIndexInfo[idx].bufferView);

	GEngine->GetGraphicsDescHeap()->CommitTable();

	GRAPHICS_CMD_LIST->DrawIndexedInstanced(_vecIndexInfo[idx].count, buffer->GetCount(), 0, 0, 0);
}

shared_ptr<Mesh> Mesh::CreateFromFBX(const FbxMeshInfo* meshInfo, FBXLoader& loader)
{
	shared_ptr<Mesh> mesh = make_shared<Mesh>();
	mesh->CreateVertexBuffer(meshInfo->vertices);

	for (const vector<uint32>& buffer : meshInfo->indices)
	{
		if (buffer.empty())
		{
			// FBX 파일이 이상하다. IndexBuffer가 없으면 에러 나니까 임시 처리
			vector<uint32> defaultBuffer{ 0 };
			mesh->CreateIndexBuffer(defaultBuffer);
		}
		else
		{
			mesh->CreateIndexBuffer(buffer);
		}
	}

	if (meshInfo->hasAnimation)
		mesh->CreateBonesAndAnimations(loader);

	return mesh;
}

void Mesh::CreateVertexBuffer(const vector<Vertex>& buffer)
{
	_vertexCount = static_cast<uint32>(buffer.size());
	uint32 bufferSize = _vertexCount * sizeof(Vertex);

	D3D12_HEAP_PROPERTIES heapProperty = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
	D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize);

	DEVICE->CreateCommittedResource(
		&heapProperty,
		D3D12_HEAP_FLAG_NONE,
		&desc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&_vertexBuffer));

	// Copy the triangle data to the vertex buffer.
	void* vertexDataBuffer = nullptr;
	CD3DX12_RANGE readRange(0, 0); // We do not intend to read from this resource on the CPU.
	_vertexBuffer->Map(0, &readRange, &vertexDataBuffer);
	::memcpy(vertexDataBuffer, &buffer[0], bufferSize);
	_vertexBuffer->Unmap(0, nullptr);

	// Initialize the vertex buffer view.
	_vertexBufferView.BufferLocation = _vertexBuffer->GetGPUVirtualAddress();
	_vertexBufferView.StrideInBytes = sizeof(Vertex); // 정점 1개 크기
	_vertexBufferView.SizeInBytes = bufferSize; // 버퍼의 크기	
}

void Mesh::CreateIndexBuffer(const vector<uint32>& buffer)
{
	uint32 indexCount = static_cast<uint32>(buffer.size());
	uint32 bufferSize = indexCount * sizeof(uint32);

	D3D12_HEAP_PROPERTIES heapProperty = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
	D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize);

	ComPtr<ID3D12Resource> indexBuffer;
	DEVICE->CreateCommittedResource(
		&heapProperty,
		D3D12_HEAP_FLAG_NONE,
		&desc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&indexBuffer));

	void* indexDataBuffer = nullptr;
	CD3DX12_RANGE readRange(0, 0);
	indexBuffer->Map(0, &readRange, &indexDataBuffer);
	::memcpy(indexDataBuffer, &buffer[0], bufferSize);
	indexBuffer->Unmap(0, nullptr);

	D3D12_INDEX_BUFFER_VIEW	indexBufferView;
	indexBufferView.BufferLocation = indexBuffer->GetGPUVirtualAddress();
	indexBufferView.Format = DXGI_FORMAT_R32_UINT;
	indexBufferView.SizeInBytes = bufferSize;

	IndexBufferInfo info =
	{
		indexBuffer,
		indexBufferView,
		DXGI_FORMAT_R32_UINT,
		indexCount
	};

	_vecIndexInfo.push_back(info);
}


void Mesh::CreateBonesAndAnimations(class FBXLoader& loader)
{
#pragma region AnimClip
	uint32 frameCount = 0;
	vector<shared_ptr<FbxAnimClipInfo>>& animClips = loader.GetAnimClip();
	for (shared_ptr<FbxAnimClipInfo>& ac : animClips)
	{
		AnimClipInfo info = {};

		info.animName = ac->name;
		info.duration = ac->endTime.GetSecondDouble() - ac->startTime.GetSecondDouble();

		int32 startFrame = static_cast<int32>(ac->startTime.GetFrameCount(ac->mode));
		int32 endFrame = static_cast<int32>(ac->endTime.GetFrameCount(ac->mode));
		info.frameCount = endFrame - startFrame;

		info.keyFrames.resize(ac->keyFrames.size());

		const int32 boneCount = static_cast<int32>(ac->keyFrames.size());
		for (int32 b = 0; b < boneCount; b++)
		{
			auto& vec = ac->keyFrames[b];

			const int32 size = static_cast<int32>(vec.size());
			frameCount = max(frameCount, static_cast<uint32>(size));
			info.keyFrames[b].resize(size);

			for (int32 f = 0; f < size; f++)
			{
				FbxKeyFrameInfo& kf = vec[f];
				// FBX에서 파싱한 정보들로 채워준다
				KeyFrameInfo& kfInfo = info.keyFrames[b][f];
				kfInfo.time = kf.time;
				kfInfo.frame = static_cast<int32>(size);
				kfInfo.scale.x = static_cast<float>(kf.matTransform.GetS().mData[0]);
				kfInfo.scale.y = static_cast<float>(kf.matTransform.GetS().mData[1]);
				kfInfo.scale.z = static_cast<float>(kf.matTransform.GetS().mData[2]);
				kfInfo.rotation.x = static_cast<float>(kf.matTransform.GetQ().mData[0]);
				kfInfo.rotation.y = static_cast<float>(kf.matTransform.GetQ().mData[1]);
				kfInfo.rotation.z = static_cast<float>(kf.matTransform.GetQ().mData[2]);
				kfInfo.rotation.w = static_cast<float>(kf.matTransform.GetQ().mData[3]);
				kfInfo.translate.x = static_cast<float>(kf.matTransform.GetT().mData[0]);
				kfInfo.translate.y = static_cast<float>(kf.matTransform.GetT().mData[1]);
				kfInfo.translate.z = static_cast<float>(kf.matTransform.GetT().mData[2]);
			}
		}

		_animClips.push_back(info);
	}
#pragma endregion

#pragma region Bones
	vector<shared_ptr<FbxBoneInfo>>& bones = loader.GetBones();
	for (shared_ptr<FbxBoneInfo>& bone : bones)
	{
		BoneInfo boneInfo = {};
		boneInfo.parentIdx = bone->parentIndex;
		boneInfo.matOffset = GetMatrix(bone->matOffset);
		boneInfo.boneName = bone->boneName;
		_bones.push_back(boneInfo);
	}
#pragma endregion

	CreateSkinBuffers();
}

Matrix Mesh::GetMatrix(FbxAMatrix& matrix)
{
	Matrix mat;

	for (int32 y = 0; y < 4; ++y)
		for (int32 x = 0; x < 4; ++x)
			mat.m[y][x] = static_cast<float>(matrix.Get(y, x));

	return mat;
}

// Builds GPU buffers once _bones/_animClips are filled. Shared by FBX and .bin.
void Mesh::CreateSkinBuffers()
{
#pragma region SkinData
	if (IsAnimMesh())
	{
		// BoneOffet 행렬
		const int32 boneCount = static_cast<int32>(_bones.size());
		vector<Matrix> offsetVec(boneCount);
		for (size_t b = 0; b < boneCount; b++)
			offsetVec[b] = _bones[b].matOffset;

		// OffsetMatrix StructuredBuffer 세팅
		_offsetBuffer = make_shared<StructuredBuffer>();
		_offsetBuffer->Init(sizeof(Matrix), static_cast<uint32>(offsetVec.size()), offsetVec.data());

		// Parent index table. Only used when the frames are local, but at 4 bytes
		// per bone it is cheap enough to always build.
		vector<int32> parentVec(boneCount);
		for (int32 b = 0; b < boneCount; b++)
			parentVec[b] = _bones[b].parentIdx;

		_boneParentBuffer = make_shared<StructuredBuffer>();
		_boneParentBuffer->Init(sizeof(int32), static_cast<uint32>(parentVec.size()), parentVec.data());

		// Concatenate every clip into a single buffer and remember where each starts.
		vector<AnimFrameParams> allFrames;
		_clipFrameOffset.clear();

		const int32 animCount = static_cast<int32>(_animClips.size());
		for (int32 i = 0; i < animCount; i++)
		{
			AnimClipInfo& animClip = _animClips[i];

			// 애니메이션 프레임 정보
			vector<AnimFrameParams> frameParams;
			frameParams.resize(_bones.size() * animClip.frameCount);

			for (int32 b = 0; b < boneCount; b++)
			{
				const int32 keyFrameCount = static_cast<int32>(animClip.keyFrames[b].size());
				for (int32 f = 0; f < keyFrameCount; f++)
				{
					int32 idx = static_cast<int32>(boneCount * f + b);

					frameParams[idx] = AnimFrameParams
					{
						Vec4(animClip.keyFrames[b][f].scale),
						animClip.keyFrames[b][f].rotation, // Quaternion
						Vec4(animClip.keyFrames[b][f].translate)
					};
				}
			}

			// StructuredBuffer 세팅
			_clipFrameOffset.push_back(static_cast<int32>(allFrames.size()));
			allFrames.insert(allFrames.end(), frameParams.begin(), frameParams.end());
		}

		if (allFrames.empty() == false)
		{
			_frameBuffer = make_shared<StructuredBuffer>();
			_frameBuffer->Init(sizeof(AnimFrameParams),
				static_cast<uint32>(allFrames.size()), allFrames.data());
		}
	}
#pragma endregion
}

// ---------------------------------------------------------------------------
// .bin (LabProject custom format) path. Produces the same result as the FBX path.
// ---------------------------------------------------------------------------

shared_ptr<Mesh> Mesh::CreateFromBin(const BinMeshInfo* meshInfo, BinLoader& loader)
{
	shared_ptr<Mesh> mesh = make_shared<Mesh>();
	mesh->SetName(meshInfo->name);
	mesh->CreateVertexBuffer(meshInfo->vertices);
	mesh->CreateBounds(meshInfo->vertices);

	for (const vector<uint32>& buffer : meshInfo->indices)
	{
		if (buffer.empty())
		{
			vector<uint32> defaultBuffer{ 0 };
			mesh->CreateIndexBuffer(defaultBuffer);
		}
		else
		{
			mesh->CreateIndexBuffer(buffer);
		}
	}

	if (meshInfo->hasAnimation)
		mesh->CreateBonesAndAnimationsFromBin(loader);

	return mesh;
}

void Mesh::CreateBonesAndAnimationsFromBin(BinLoader& loader)
{
	// --- AnimClip ---
	// BinLoader already decomposed the matrices into SRT, so just copy them over.
	vector<shared_ptr<BinAnimClipInfo>>& animClips = loader.GetAnimClip();
	for (shared_ptr<BinAnimClipInfo>& ac : animClips)
	{
		if (ac == nullptr)
			continue;

		AnimClipInfo info = {};
		info.animName = ac->name;
		info.duration = ac->duration;
		info.frameCount = ac->frameCount;

		const int32 boneCount = static_cast<int32>(ac->keyFrames.size());
		info.keyFrames.resize(boneCount);

		for (int32 b = 0; b < boneCount; b++)
		{
			const int32 size = static_cast<int32>(ac->keyFrames[b].size());
			info.keyFrames[b].resize(size);

			for (int32 f = 0; f < size; f++)
			{
				const BinKeyFrameInfo& src = ac->keyFrames[b][f];
				KeyFrameInfo& dst = info.keyFrames[b][f];

				dst.time = src.time;
				dst.frame = src.frame;
				dst.scale = src.scale;
				dst.rotation = src.rotation;
				dst.translate = src.translate;
			}
		}

		_animClips.push_back(info);
	}

	// --- Bones ---
	vector<shared_ptr<BinBoneInfo>>& bones = loader.GetBones();
	for (shared_ptr<BinBoneInfo>& bone : bones)
	{
		BoneInfo boneInfo = {};
		boneInfo.boneName = bone->boneName;
		boneInfo.parentIdx = bone->parentIndex;
		// The compute shader reads g_offset column-major. The FBX path copies
		// FbxAMatrix straight through and already lands in that form, but .bin
		// offsets are row-major, so match the convention here.
		boneInfo.matOffset = bone->matOffset.Transpose();
		_bones.push_back(boneInfo);
	}

	// BinLoader bakes the hierarchy into the clips at load time, so the frames
	// arriving here are already in model space (same shape as the FBX path).
	_boneFramesLocal = true;

	CreateSkinBuffers();
}
