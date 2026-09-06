#pragma once

// ************************
// GraphicsDescriptorHeap
// ************************

class GraphicsDescriptorHeap
{
public:
	void Init(uint32 count);

	void Clear();
	void SetCBV(D3D12_CPU_DESCRIPTOR_HANDLE srcHandle, CBV_REGISTER reg);
	void SetSRV(D3D12_CPU_DESCRIPTOR_HANDLE srcHandle, SRV_REGISTER reg);

	void CommitTable();

	ComPtr<ID3D12DescriptorHeap> GetDescriptorHeap() { return _descHeap; }

	D3D12_CPU_DESCRIPTOR_HANDLE GetCPUHandle(CBV_REGISTER reg);
	D3D12_CPU_DESCRIPTOR_HANDLE GetCPUHandle(SRV_REGISTER reg);

private:
	D3D12_CPU_DESCRIPTOR_HANDLE GetCPUHandle(uint8 reg);

	// 그룹을 다 쓰면 힙 밖을 가리키게 된다. CopyDescriptors 는 힙 할당 밖을 쓰고,
	// SetGraphicsRootDescriptorTable 은 힙 밖 GPU 핸들을 묶어 디바이스를 날린다.
	// 마지막 그룹을 겹쳐 쓰면 그림은 틀리지만 살아서 원인을 볼 수 있다.
	uint32 SafeGroupIndex();

private:

	ComPtr<ID3D12DescriptorHeap> _descHeap;
	uint64					_handleSize = 0;
	uint64					_groupSize = 0;
	uint64					_groupCount = 0;

	uint32					_currentGroupIndex = 0;
};


// ************************
// ComputeDescriptorHeap
// ************************

// 한 프레임에 디스패치 여러 개를 한 커맨드 리스트에 쌓으므로,
// 디스크립터도 디스패치마다 다른 자리에 있어야 한다.
//
// GPU 는 디스크립터를 '기록할 때' 가 아니라 '실행할 때' 읽는다.
// 자리 하나를 돌려 쓰면 모든 디스패치가 마지막에 쓴 디스크립터를 보게 된다.
// 예전에는 디스패치마다 CPU 가 GPU 를 기다렸기 때문에 자리 하나로 버텼다.
class ComputeDescriptorHeap
{
public:
	void Init(uint32 count);

	void Clear();
	void SetCBV(D3D12_CPU_DESCRIPTOR_HANDLE srcHandle, CBV_REGISTER reg);
	void SetSRV(D3D12_CPU_DESCRIPTOR_HANDLE srcHandle, SRV_REGISTER reg);
	void SetUAV(D3D12_CPU_DESCRIPTOR_HANDLE srcHandle, UAV_REGISTER reg);

	void CommitTable();

	D3D12_CPU_DESCRIPTOR_HANDLE GetCPUHandle(CBV_REGISTER reg);
	D3D12_CPU_DESCRIPTOR_HANDLE GetCPUHandle(SRV_REGISTER reg);
	D3D12_CPU_DESCRIPTOR_HANDLE GetCPUHandle(UAV_REGISTER reg);

private:
	D3D12_CPU_DESCRIPTOR_HANDLE GetCPUHandle(uint8 reg);

	// 그래픽스 쪽과 같은 이유. 위 주석 참조.
	uint32 SafeGroupIndex();

private:

	ComPtr<ID3D12DescriptorHeap> _descHeap;
	uint64						_handleSize = 0;
	uint64						_groupSize = 0;
	uint64						_groupCount = 0;

	uint32						_currentGroupIndex = 0;
};


