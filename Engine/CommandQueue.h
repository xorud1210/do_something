#pragma once

class SwapChain;
class DescriptorHeap;

// ************************
// GraphicsCommandQueue
// ************************

class GraphicsCommandQueue
{
public:
	~GraphicsCommandQueue();

	void Init(ComPtr<ID3D12Device> device, shared_ptr<SwapChain> swapChain);
	void WaitSync();

	void RenderBegin();
	void RenderEnd();

	void FlushResourceCommandQueue();

	ComPtr<ID3D12CommandQueue> GetCmdQueue() { return _cmdQueue; }
	ComPtr<ID3D12GraphicsCommandList> GetGraphicsCmdList() { return	_cmdList; }
	ComPtr<ID3D12GraphicsCommandList> GetResourceCmdList() { return	_resCmdList; }

private:
	ComPtr<ID3D12CommandQueue>			_cmdQueue;
	ComPtr<ID3D12CommandAllocator>		_cmdAlloc;
	ComPtr<ID3D12GraphicsCommandList>	_cmdList;

	ComPtr<ID3D12CommandAllocator>		_resCmdAlloc;
	ComPtr<ID3D12GraphicsCommandList>	_resCmdList;

	ComPtr<ID3D12Fence>					_fence;
	uint32								_fenceValue = 0;
	HANDLE								_fenceEvent = INVALID_HANDLE_VALUE;

	shared_ptr<SwapChain>		_swapChain;
};

// ************************
// ComputeCommandQueue
// ************************

// 이 큐는 정리할 OS 핸들이 없다.
// 예전에는 CPU 를 세우려고 이벤트 핸들을 들고 있었는데, 지금은 펜스 값만
// 올리고 그래픽스 큐가 기다리므로 소멸자가 할 일이 없다.
class ComputeCommandQueue
{
public:
	void Init(ComPtr<ID3D12Device> device);

	// 프레임 시작. 이번 프레임에 쌓을 컴퓨트 명령을 받을 준비를 한다.
	void Begin();

	// 쌓인 명령을 한 번에 제출하고 펜스만 올린다. CPU 는 기다리지 않는다.
	void Submit();

	// 그래픽스 큐가 이 펜스를 기다린다.
	ID3D12Fence* GetFence() { return _fence.Get(); }
	uint64 GetFenceValue() const { return _fenceValue; }

	ComPtr<ID3D12CommandQueue> GetCmdQueue() { return _cmdQueue; }
	ComPtr<ID3D12GraphicsCommandList> GetComputeCmdList() { return _cmdList; }

private:
	ComPtr<ID3D12CommandQueue>			_cmdQueue;
	ComPtr<ID3D12CommandAllocator>		_cmdAlloc;
	ComPtr<ID3D12GraphicsCommandList>	_cmdList;

	ComPtr<ID3D12Fence>					_fence;
	uint64								_fenceValue = 0;
};