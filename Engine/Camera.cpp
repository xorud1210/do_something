#include "pch.h"
#include "Camera.h"
#include "Transform.h"
#include "Scene.h"
#include "SceneManager.h"
#include "BillboardRenderer.h"
#include "GameObject.h"
#include "MeshRenderer.h"
#include "Engine.h"
#include "Material.h"
#include "Shader.h"
#include "ParticleSystem.h"
#include "InstancingManager.h"

Matrix Camera::S_MatView;
Matrix Camera::S_MatProjection;

Camera::Camera() : Component(COMPONENT_TYPE::CAMERA)
{
	_width = static_cast<float>(GEngine->GetWindow().width);
	_height = static_cast<float>(GEngine->GetWindow().height);
}

Camera::~Camera()
{
}

void Camera::FinalUpdate()
{
	_matView = GetTransform()->GetLocalToWorldMatrix().Invert();

	if (_type == PROJECTION_TYPE::PERSPECTIVE)
		_matProjection = ::XMMatrixPerspectiveFovLH(_fov, _width / _height, _near, _far);
	else
		_matProjection = ::XMMatrixOrthographicLH(_width * _scale, _height * _scale, _near, _far);

	_frustum.FinalUpdate();
}

void Camera::SortGameObject()
{
	shared_ptr<Scene> scene = GET_SINGLE(SceneManager)->GetActiveScene();
	const vector<shared_ptr<GameObject>>& gameObjects = scene->GetGameObjects();

	_vecForward.clear();
	_vecDeferred.clear();
	_vecParticle.clear();
	_vecBillboard.clear();

	for (auto& gameObject : gameObjects)
	{
		if (gameObject->GetMeshRenderer() == nullptr
			&& gameObject->GetParticleSystem() == nullptr
			&& gameObject->GetBillboardRenderer() == nullptr)
			continue;

		if (IsCulled(gameObject->GetLayerIndex()))
			continue;

		if (gameObject->GetCheckFrustum())
		{
			if (_frustum.ContainsSphere(
				gameObject->GetTransform()->GetWorldPosition(),
				gameObject->GetTransform()->GetBoundingSphereRadius()) == false)
			{
				continue;
			}
		}

		if (gameObject->GetMeshRenderer())
		{
			SHADER_TYPE shaderType = gameObject->GetMeshRenderer()->GetMaterial()->GetShader()->GetShaderType();
			switch (shaderType)
			{
			case SHADER_TYPE::DEFERRED:
				_vecDeferred.push_back(gameObject);
				break;
			case SHADER_TYPE::FORWARD:
			case SHADER_TYPE::SWAP_CHAIN:
				// 그리는 방식은 포워드와 같다. 대상 렌더타겟만 백버퍼로 다르고,
				// 그건 Scene 이 어느 패스에서 부르느냐로 갈린다.
				_vecForward.push_back(gameObject);
				break;
			}
		}
		else if (gameObject->GetBillboardRenderer())
		{
			_vecBillboard.push_back(gameObject);
		}
		else
		{
			_vecParticle.push_back(gameObject);
		}
	}
}

void Camera::SortShadowObject()
{
	shared_ptr<Scene> scene = GET_SINGLE(SceneManager)->GetActiveScene();
	const vector<shared_ptr<GameObject>>& gameObjects = scene->GetGameObjects();

	_vecShadow.clear();
	_vecShadowBillboard.clear();

	for (auto& gameObject : gameObjects)
	{
		// 빌보드는 셰도우 패스에 넣을지를 자기가 정한다.
		// 절두체 검사는 하지 않는다 - 여기서 쓰는 절두체는 광원의 것이고,
		// 초목은 심은 범위 전체가 한 오브젝트라 잘라 봐야 전부 아니면 전무다.
		if (gameObject->GetBillboardRenderer())
		{
			if (IsCulled(gameObject->GetLayerIndex()) == false
				&& gameObject->GetBillboardRenderer()->IsCastShadow())
			{
				_vecShadowBillboard.push_back(gameObject);
			}
			continue;
		}

		if (gameObject->GetMeshRenderer() == nullptr)
			continue;

		if (gameObject->IsStatic())
			continue;

		if (IsCulled(gameObject->GetLayerIndex()))
			continue;

		if (gameObject->GetCheckFrustum())
		{
			if (_frustum.ContainsSphere(
				gameObject->GetTransform()->GetWorldPosition(),
				gameObject->GetTransform()->GetBoundingSphereRadius()) == false)
			{
				continue;
			}
		}

		_vecShadow.push_back(gameObject);
	}
}

void Camera::Render_Deferred()
{
	S_MatView = _matView;
	S_MatProjection = _matProjection;

	GET_SINGLE(InstancingManager)->Render(_vecDeferred);

	// 빌보드도 G-Buffer 에 쓴다. 알파 블렌딩이 아니라 알파 테스트라 디퍼드로 갈 수 있고,
	// 그 덕에 조명과 그림자를 그대로 받는다.
	for (auto& gameObject : _vecBillboard)
	{
		gameObject->GetBillboardRenderer()->Render();
	}
}

void Camera::Render_Forward()
{
	S_MatView = _matView;
	S_MatProjection = _matProjection;

	GET_SINGLE(InstancingManager)->Render(_vecForward);

	for (auto& gameObject : _vecParticle)
	{
		gameObject->GetParticleSystem()->Render();
	}
}

void Camera::Render_Shadow(uint32 cascade)
{
	S_MatView = _matView;
	S_MatProjection = _matProjection;

	for (auto& gameObject : _vecShadow)
	{
		gameObject->GetMeshRenderer()->RenderShadow();
	}

	for (auto& gameObject : _vecShadowBillboard)
	{
		gameObject->GetBillboardRenderer()->RenderShadow(cascade);
	}
}