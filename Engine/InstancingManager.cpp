#include "pch.h"
#include "InstancingManager.h"
#include "InstancingBuffer.h"
#include "GameObject.h"
#include "MeshRenderer.h"
#include "Transform.h"
#include "Camera.h"

void InstancingManager::Render(vector<shared_ptr<GameObject>>& gameObjects)
{
	map<uint64, vector<shared_ptr<GameObject>>> cache;

	for (shared_ptr<GameObject>& gameObject : gameObjects)
	{
		const uint64 instanceId = gameObject->GetMeshRenderer()->GetInstanceID();
		cache[instanceId].push_back(gameObject);
	}

	for (auto& pair : cache)
	{
		const vector<shared_ptr<GameObject>>& vec = pair.second;

		if (vec.size() == 1)
		{
			vec[0]->GetMeshRenderer()->Render();
		}
		else
		{
			shared_ptr<InstancingBuffer> buffer = Take();

			for (const shared_ptr<GameObject>& gameObject : vec)
			{
				InstancingParams params;
				params.matWorld = gameObject->GetTransform()->GetLocalToWorldMatrix();
				params.matWV = params.matWorld * Camera::S_MatView;
				params.matWVP = params.matWorld * Camera::S_MatView * Camera::S_MatProjection;

				buffer->AddData(params);
			}

			vec[0]->GetMeshRenderer()->Render(buffer);
		}
	}
}

shared_ptr<InstancingBuffer> InstancingManager::Take()
{
	if (_used >= _pool.size())
	{
		shared_ptr<InstancingBuffer> buffer = make_shared<InstancingBuffer>();
		buffer->Init();
		_pool.push_back(buffer);
	}

	shared_ptr<InstancingBuffer> buffer = _pool[_used];
	_used++;

	buffer->Clear();
	return buffer;
}

void InstancingManager::ClearBuffer()
{
	// 되감기만 한다. 버퍼 자체는 Take 가 꺼낼 때 비운다.
	//
	// 이 되감기가 프레임 머리에 있어야 하는 이유는 GPU 때문이다.
	// 버퍼는 UPLOAD 힙이라 CPU 가 직접 덮어쓰는데, 직전 프레임의 GPU 작업이
	// 끝나 있어야 안전하다. 그 보장은 RenderEnd 의 WaitSync 하나뿐이다.
	_used = 0;
}
