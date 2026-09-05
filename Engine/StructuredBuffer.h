#pragma once

class StructuredBuffer
{
public:
	StructuredBuffer();
	~StructuredBuffer();

	void Init(uint32 elementSize, uint32 elementCount, void* initialData = nullptr);

	// 매 프레임 내용이 바뀌는 버퍼.
	//
	// Init 의 초기 데이터 경로는 UPLOAD 버퍼를 만들어 DEFAULT 힙으로 복사한 뒤
	// 커맨드 큐를 통째로 비운다(FlushResourceCommandQueue). 프레임마다 그러면
	// CPU 와 GPU 가 매번 만나게 되어 쓸 수가 없다.
	//
	// GPU 가 읽기만 하는 데이터라면 UPLOAD 힙에 그대로 두고 CPU 가 직접 쓰면 된다.
	// 이 엔진은 RenderEnd 에서 매 프레임 WaitSync 로 GPU 를 기다리므로,
	// 이전 프레임이 아직 읽고 있을 걱정 없이 한 장만 두고 덮어써도 된다.
	void InitDynamic(uint32 elementSize, uint32 elementCount);
	void UpdateDynamic(const void* data, uint32 elementCount);

	void PushGraphicsData(SRV_REGISTER reg);
	void PushComputeSRVData(SRV_REGISTER reg);
	void PushComputeUAVData(UAV_REGISTER reg);

	ComPtr<ID3D12DescriptorHeap> GetSRV() { return _srvHeap; }
	ComPtr<ID3D12DescriptorHeap> GetUAV() { return _uavHeap; }

	void SetResourceState(D3D12_RESOURCE_STATES state) { _resourceState = state; }
	D3D12_RESOURCE_STATES GetResourceState() { return _resourceState; }
	ComPtr<ID3D12Resource> GetBuffer() { return _buffer; }

	uint32	GetElementSize() { return _elementSize; }
	uint32	GetElementCount() { return _elementCount; }
	UINT	GetBufferSize() { return _elementSize * _elementCount; }

private:
	void CopyInitialData(uint64 bufferSize, void* initialData);
	void CreateSRV();

private:
	ComPtr<ID3D12Resource>			_buffer;
	ComPtr<ID3D12DescriptorHeap>	_srvHeap;
	ComPtr<ID3D12DescriptorHeap>	_uavHeap;

	uint32						_elementSize = 0;
	uint32						_elementCount = 0;
	D3D12_RESOURCE_STATES		_resourceState = {};
	uint8*						_mappedData = nullptr;	// InitDynamic 일 때만

private:
	D3D12_CPU_DESCRIPTOR_HANDLE _srvHeapBegin = {};
	D3D12_CPU_DESCRIPTOR_HANDLE _uavHeapBegin = {};
};

