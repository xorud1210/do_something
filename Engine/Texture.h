#pragma once
#include "Object.h"

class Texture : public Object
{
public:
	Texture();
	virtual ~Texture();

	// 색으로 쓰는 텍스처는 sRGB 로 저장돼 있다.
	// 포맷을 sRGB 계열로 지정해 만들면 샘플링할 때 하드웨어가 선형으로 풀어준다.
	// 노멀맵·높이맵처럼 색이 아닌 데이터는 절대 이걸 켜면 안 된다 -
	// 저장된 숫자가 그대로 의미인데 곡선을 먹이면 값이 바뀐다.
	virtual void Load(const wstring& path) override;
	void Load(const wstring& path, bool srgb);

public:
	void Create(DXGI_FORMAT format, uint32 width, uint32 height,
		const D3D12_HEAP_PROPERTIES& heapProperty, D3D12_HEAP_FLAGS heapFlags,
		D3D12_RESOURCE_FLAGS resFlags, Vec4 clearColor = Vec4());

	void CreateFromResource(ComPtr<ID3D12Resource> tex2D);

public:
	ComPtr<ID3D12Resource> GetTex2D() { return _tex2D; }
	ComPtr<ID3D12DescriptorHeap> GetSRV() { return _srvHeap; }
	ComPtr<ID3D12DescriptorHeap> GetRTV() { return _rtvHeap; }
	ComPtr<ID3D12DescriptorHeap> GetDSV() { return _dsvHeap; }
	ComPtr<ID3D12DescriptorHeap> GetUAV() { return _uavHeap; }

	D3D12_CPU_DESCRIPTOR_HANDLE GetSRVHandle() { return _srvHeapBegin; }
	D3D12_CPU_DESCRIPTOR_HANDLE GetUAVHandle() { return _uavHeapBegin; }

	float GetWidth() { return static_cast<float>(_desc.Width); }
	float GetHeight() { return static_cast<float>(_desc.Height); }

private:
	ScratchImage			 		_image;
	D3D12_RESOURCE_DESC				_desc;
	ComPtr<ID3D12Resource>			_tex2D;

	ComPtr<ID3D12DescriptorHeap>	_srvHeap;
	ComPtr<ID3D12DescriptorHeap>	_rtvHeap;
	ComPtr<ID3D12DescriptorHeap>	_dsvHeap;
	ComPtr<ID3D12DescriptorHeap>	_uavHeap;

private:
	D3D12_CPU_DESCRIPTOR_HANDLE		_srvHeapBegin = {};
	D3D12_CPU_DESCRIPTOR_HANDLE		_uavHeapBegin = {};
};

