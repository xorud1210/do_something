#pragma once
#include "Object.h"

class Shader;
class Texture;

enum
{
	MATERIAL_ARG_COUNT = 4,
};

struct MaterialParams
{
	MaterialParams()
	{
		for (int32 i = 0; i < MATERIAL_ARG_COUNT; i++)
		{
			SetInt(i, 0);
			SetFloat(i, 0.f);
			SetTexOn(i, 0);
		}
	}

	// 배열들이 구조체 안에 나란히 붙어 있어서, 범위를 넘기면 옆 배열을 조용히
	// 덮어쓴다. 크래시도 안 나고 그림만 틀리므로 여기서 막는다.
	static bool InRange(uint8 index) { assert(index < MATERIAL_ARG_COUNT); return index < MATERIAL_ARG_COUNT; }

	void SetInt(uint8 index, int32 value) { if (InRange(index)) intParams[index] = value; }
	void SetFloat(uint8 index, float value) { if (InRange(index)) floatParams[index] = value; }
	void SetTexOn(uint8 index, int32 value) { if (InRange(index)) texOnParams[index] = value; }
	void SetVec2(uint8 index, Vec2 value) { if (InRange(index)) vec2Params[index] = value; }
	void SetVec4(uint8 index, Vec4 value) { if (InRange(index)) vec4Params[index] = value; }
	void SetMatrix(uint8 index, Matrix& value) { if (InRange(index)) matrixParams[index] = value; }

	array<int32, MATERIAL_ARG_COUNT> intParams;
	array<float, MATERIAL_ARG_COUNT> floatParams;
	array<int32, MATERIAL_ARG_COUNT> texOnParams;
	array<Vec2, MATERIAL_ARG_COUNT> vec2Params;
	array<Vec4, MATERIAL_ARG_COUNT> vec4Params;
	array<Matrix, MATERIAL_ARG_COUNT> matrixParams;
};

class Material : public Object
{
public:
	Material();
	virtual ~Material();

	shared_ptr<Shader> GetShader() { return _shader; }

	void SetShader(shared_ptr<Shader> shader) { _shader = shader; }
	void SetInt(uint8 index, int32 value) { _params.SetInt(index, value); }
	void SetFloat(uint8 index, float value) { _params.SetFloat(index, value); }
	shared_ptr<Texture> GetTexture(uint8 index) const
	{
		return MaterialParams::InRange(index) ? _textures[index] : nullptr;
	}

	void SetTexture(uint8 index, shared_ptr<Texture> texture)
	{
		if (MaterialParams::InRange(index) == false)
			return;

		_textures[index] = texture;
		_params.SetTexOn(index, (texture == nullptr ? 0 : 1));
	}

	void SetVec2(uint8 index, Vec2 value) { _params.SetVec2(index, value); }
	void SetVec4(uint8 index, Vec4 value) { _params.SetVec4(index, value); }
	void SetMatrix(uint8 index, Matrix& value) { _params.SetMatrix(index, value); }

	void PushGraphicsData();
	void PushComputeData();
	void Dispatch(uint32 x, uint32 y, uint32 z);

	shared_ptr<Material> Clone();

private:
	shared_ptr<Shader>	_shader;
	MaterialParams		_params;
	array<shared_ptr<Texture>, MATERIAL_ARG_COUNT> _textures;
};

