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
	//
	// 그 대기를 없앨 거면(프레임 버퍼링) 여기도 프레임 수만큼 돌려 써야 한다.
	void InitDynamic(uint32 elementSize, uint32 elementCount);
	void UpdateDynamic(const void* data, uint32 elementCount);

	void PushGraphicsData(SRV_REGISTER reg);
	void PushComputeSRVData(SRV_REGISTER reg);
	void PushComputeUAVData(UAV_REGISTER reg);

	ComPtr<ID3D12DescriptorHeap> GetSRV() { return _srvHeap; }
	ComPtr<ID3D12DescriptorHeap> GetUAV() { return _uavHeap; }

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
	// 생성 시점의 상태일 뿐이다. 이후를 추적하지 않는다.
	//
	// 예전에는 Set/GetResourceState 가 붙어 있어서 상태를 추적하는 것처럼
	// 보였지만 아무도 부르지 않았고 갱신도 되지 않았다. 배리어를 짜면서
	// 이 값을 믿으면 틀린다. D3D12 에서 버퍼는 ExecuteCommandLists 가
	// 끝나면 큐 종류와 무관하게 COMMON 으로 돌아오므로(decay),
	// 커맨드 리스트 안에서만 명시적으로 전이시키면 된다.
	D3D12_RESOURCE_STATES		_initialState = {};
	uint8*						_mappedData = nullptr;	// InitDynamic 일 때만

private:
	D3D12_CPU_DESCRIPTOR_HANDLE _srvHeapBegin = {};
	D3D12_CPU_DESCRIPTOR_HANDLE _uavHeapBegin = {};
};

