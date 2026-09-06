#include "pch.h"
#include "InstancingBuffer.h"
#include "Engine.h"

InstancingBuffer::InstancingBuffer()
{
}

InstancingBuffer::~InstancingBuffer()
{
}

void InstancingBuffer::Init(uint32 maxCount)
{
	_maxCount = maxCount;

	const int32 bufferSize = sizeof(InstancingParams) * maxCount;
	D3D12_HEAP_PROPERTIES heapProperty = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
	D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize);

	DEVICE->CreateCommittedResource(
		&heapProperty,
		D3D12_HEAP_FLAG_NONE,
		&desc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&_buffer));
}

void InstancingBuffer::Clear()
{
	_data.clear();
}

void InstancingBuffer::AddData(InstancingParams& params)
{
	_data.push_back(params);
}

// 여기는 프레임 끝의 WaitSync 에 기대고 있다.
//
// _buffer 는 UPLOAD 힙이고 CPU 가 직접 memcpy 로 덮어쓴다. 재할당도 그 자리에서
// 이전 리소스를 놓는다. 둘 다 "직전 프레임 GPU 작업이 끝나 있다" 가 전제인데,
// 그 보장은 GraphicsCommandQueue::RenderEnd 의 WaitSync 하나뿐이다.
//
// 프레임 버퍼링을 붙여 그 대기를 없앨 거면 여기도 같이 손봐야 한다
// (프레임 수만큼 버퍼를 돌려 쓰는 식으로).
void InstancingBuffer::PushData()
{
	const uint32 dataCount = GetCount();
	if (dataCount == 0)
		return;

	if (dataCount > _maxCount)
		Init(dataCount);

	const uint32 bufferSize = dataCount * sizeof(InstancingParams);

	void* dataBuffer = nullptr;
	D3D12_RANGE readRange{ 0, 0 };
	_buffer->Map(0, &readRange, &dataBuffer);
	memcpy(dataBuffer, &_data[0], bufferSize);
	_buffer->Unmap(0, nullptr);

	_bufferView.BufferLocation = _buffer->GetGPUVirtualAddress();
	_bufferView.StrideInBytes = sizeof(InstancingParams);
	_bufferView.SizeInBytes = bufferSize;
}