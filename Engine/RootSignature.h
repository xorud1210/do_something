#pragma once

class RootSignature
{
public:
	void Init();

	ComPtr<ID3D12RootSignature>	GetGraphicsRootSignature() { return _graphicsRootSignature; }
	ComPtr<ID3D12RootSignature>	GetComputeRootSignature() { return _computeRootSignature; }

private:
	void CreateGraphicsRootSignature();
	void CreateComputeRootSignature();

private:
	// s0 : Anisotropic + Wrap (기본)
	// s1 : Linear + Clamp. 화면 전체를 훑는 패스가 가장자리에서 반대편을 물어오지 않게 한다
	// s2 : Point + Clamp. 셰도우 맵 비교용 - 깊이를 보간한 뒤 비교하면 값이 무의미해진다
	D3D12_STATIC_SAMPLER_DESC	_samplerDesc[3];
	ComPtr<ID3D12RootSignature>	_graphicsRootSignature;	
	ComPtr<ID3D12RootSignature>	_computeRootSignature;
};

