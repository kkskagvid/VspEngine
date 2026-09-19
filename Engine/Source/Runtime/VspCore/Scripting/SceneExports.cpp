#include "RuntimePCH.h"

#include "Classes/Component.h"
#include "Classes/GameObject.h"
#include "Classes/Scene.h"
#include "Classes/Transform.h"
#include "Scripting/ScriptExport.h"

// -------------------------------------------------------------------------
// Scene exports consumed by managed code (C# -> C++ direction).
// VspEngine's Object / GameObject / Component / Transform classes P/Invoke
// these exact names from VspCore.dll. A managed engine object stores nothing
// but the handle it was created with, so every accessor here is the real
// implementation of the corresponding managed property.
// Everything is plain data in/out - no exceptions cross the boundary.
// -------------------------------------------------------------------------

namespace
{
	// Copies UTF-8 text into a caller buffer, always terminating it. Returns
	// the number of bytes written without the terminator (0 when the handle or
	// the buffer is unusable).
	int32 CopyTextToBuffer(const Vsp::VspString& sText, char* pBufferUtf8, int32 nBufferCapacityBytes)
	{
		if (pBufferUtf8 == nullptr || nBufferCapacityBytes <= 0)
		{
			return 0;
		}

		const size_t nTextByteCount = sText.GetByteLength();
		const size_t nMaxCopyByteCount = static_cast<size_t>(nBufferCapacityBytes) - 1u;
		const size_t nCopyByteCount = nTextByteCount < nMaxCopyByteCount ? nTextByteCount : nMaxCopyByteCount;

		if (nCopyByteCount > 0)
		{
			memcpy(pBufferUtf8, sText.GetData(), nCopyByteCount);
		}
		pBufferUtf8[nCopyByteCount] = '\0';
		return static_cast<int32>(nCopyByteCount);
	}

	// Builds a VspString from caller UTF-8 text, tolerating a null pointer.
	Vsp::VspString MakeText(const char* pTextUtf8)
	{
		return pTextUtf8 != nullptr ? Vsp::VspString(pTextUtf8) : Vsp::VspString();
	}
}

// -------- Generic object access (VspEngine.Object) --------

CSHARP_EXPORT int32 VspObject_IsValid(uint32 uHandle)
{
	return Vsp::Scene::Get().IsValidHandle(uHandle) ? 1 : 0;
}

CSHARP_EXPORT int32 VspObject_GetName(uint32 uHandle, char* pBufferUtf8, int32 nBufferCapacityBytes)
{
	const Vsp::Scene& scene = Vsp::Scene::Get();
	if (!scene.IsValidHandle(uHandle))
	{
		return CopyTextToBuffer(Vsp::VspString(), pBufferUtf8, nBufferCapacityBytes);
	}
	return CopyTextToBuffer(scene.GetObjectName(uHandle), pBufferUtf8, nBufferCapacityBytes);
}

CSHARP_EXPORT int32 VspObject_SetName(uint32 uHandle, const char* pNameUtf8)
{
	return Vsp::Scene::Get().SetObjectName(uHandle, MakeText(pNameUtf8)) ? 1 : 0;
}

// -------- Game objects --------

CSHARP_EXPORT uint32 VspGameObject_Create(const char* pNameUtf8)
{
	return Vsp::Scene::Get().CreateGameObject(MakeText(pNameUtf8));
}

CSHARP_EXPORT int32 VspGameObject_Destroy(uint32 uGameObjectHandle)
{
	return Vsp::Scene::Get().DestroyGameObject(uGameObjectHandle) ? 1 : 0;
}

CSHARP_EXPORT uint32 VspGameObject_GetTransform(uint32 uGameObjectHandle)
{
	return Vsp::Scene::Get().FindGameObjectTransformHandle(uGameObjectHandle);
}

CSHARP_EXPORT int32 VspGameObject_GetActiveSelf(uint32 uGameObjectHandle)
{
	const Vsp::GameObject* pGameObject = Vsp::Scene::Get().FindGameObject(uGameObjectHandle);
	return (pGameObject != nullptr && pGameObject->IsActiveSelf()) ? 1 : 0;
}

CSHARP_EXPORT void VspGameObject_SetActiveSelf(uint32 uGameObjectHandle, int32 bIsActive)
{
	Vsp::GameObject* pGameObject = Vsp::Scene::Get().FindGameObject(uGameObjectHandle);
	if (pGameObject != nullptr)
	{
		pGameObject->SetActiveSelf(bIsActive != 0);
	}
}

CSHARP_EXPORT int32 VspGameObject_GetLayer(uint32 uGameObjectHandle)
{
	const Vsp::GameObject* pGameObject = Vsp::Scene::Get().FindGameObject(uGameObjectHandle);
	return pGameObject != nullptr ? pGameObject->GetLayer() : 0;
}

CSHARP_EXPORT void VspGameObject_SetLayer(uint32 uGameObjectHandle, int32 nLayer)
{
	Vsp::GameObject* pGameObject = Vsp::Scene::Get().FindGameObject(uGameObjectHandle);
	if (pGameObject != nullptr)
	{
		pGameObject->SetLayer(nLayer);
	}
}

// -------- Transforms --------

CSHARP_EXPORT uint32 VspTransform_GetOwnerGameObject(uint32 uTransformHandle)
{
	const Vsp::Transform* pTransform = Vsp::Scene::Get().FindTransform(uTransformHandle);
	return pTransform != nullptr ? pTransform->GetOwnerGameObjectHandle() : Vsp::k_nInvalidObjectHandle;
}

CSHARP_EXPORT void VspTransform_GetLocalPosition(uint32 uTransformHandle, float* pOutXyz)
{
	if (pOutXyz == nullptr)
	{
		return;
	}

	const Vsp::Transform* pTransform = Vsp::Scene::Get().FindTransform(uTransformHandle);
	if (pTransform == nullptr)
	{
		pOutXyz[0] = 0.0f;
		pOutXyz[1] = 0.0f;
		pOutXyz[2] = 0.0f;
		return;
	}
	pTransform->GetLocalPosition(pOutXyz[0], pOutXyz[1], pOutXyz[2]);
}

CSHARP_EXPORT void VspTransform_SetLocalPosition(uint32 uTransformHandle, float fPositionX, float fPositionY, float fPositionZ)
{
	Vsp::Transform* pTransform = Vsp::Scene::Get().FindTransform(uTransformHandle);
	if (pTransform != nullptr)
	{
		pTransform->SetLocalPosition(fPositionX, fPositionY, fPositionZ);
	}
}

CSHARP_EXPORT void VspTransform_GetLocalRotation(uint32 uTransformHandle, float* pOutXyz)
{
	if (pOutXyz == nullptr)
	{
		return;
	}

	const Vsp::Transform* pTransform = Vsp::Scene::Get().FindTransform(uTransformHandle);
	if (pTransform == nullptr)
	{
		pOutXyz[0] = 0.0f;
		pOutXyz[1] = 0.0f;
		pOutXyz[2] = 0.0f;
		return;
	}
	pTransform->GetLocalRotation(pOutXyz[0], pOutXyz[1], pOutXyz[2]);
}

CSHARP_EXPORT void VspTransform_SetLocalRotation(uint32 uTransformHandle, float fRotationX, float fRotationY, float fRotationZ)
{
	Vsp::Transform* pTransform = Vsp::Scene::Get().FindTransform(uTransformHandle);
	if (pTransform != nullptr)
	{
		pTransform->SetLocalRotation(fRotationX, fRotationY, fRotationZ);
	}
}

CSHARP_EXPORT void VspTransform_GetLocalScale(uint32 uTransformHandle, float* pOutXyz)
{
	if (pOutXyz == nullptr)
	{
		return;
	}

	const Vsp::Transform* pTransform = Vsp::Scene::Get().FindTransform(uTransformHandle);
	if (pTransform == nullptr)
	{
		pOutXyz[0] = 1.0f;
		pOutXyz[1] = 1.0f;
		pOutXyz[2] = 1.0f;
		return;
	}
	pTransform->GetLocalScale(pOutXyz[0], pOutXyz[1], pOutXyz[2]);
}

CSHARP_EXPORT void VspTransform_SetLocalScale(uint32 uTransformHandle, float fScaleX, float fScaleY, float fScaleZ)
{
	Vsp::Transform* pTransform = Vsp::Scene::Get().FindTransform(uTransformHandle);
	if (pTransform != nullptr)
	{
		pTransform->SetLocalScale(fScaleX, fScaleY, fScaleZ);
	}
}

CSHARP_EXPORT int32 VspTransform_IsDirty(uint32 uTransformHandle)
{
	const Vsp::Transform* pTransform = Vsp::Scene::Get().FindTransform(uTransformHandle);
	return (pTransform != nullptr && pTransform->IsDirty()) ? 1 : 0;
}

CSHARP_EXPORT void VspTransform_ClearDirtyFlag(uint32 uTransformHandle)
{
	Vsp::Transform* pTransform = Vsp::Scene::Get().FindTransform(uTransformHandle);
	if (pTransform != nullptr)
	{
		pTransform->ClearDirtyFlag();
	}
}

// -------- Components --------

CSHARP_EXPORT uint32 VspComponent_GetOwnerGameObject(uint32 uComponentHandle)
{
	const Vsp::Component* pComponent = Vsp::Scene::Get().FindComponent(uComponentHandle);
	return pComponent != nullptr ? pComponent->GetOwnerGameObjectHandle() : Vsp::k_nInvalidObjectHandle;
}

CSHARP_EXPORT int32 VspComponent_IsEnabled(uint32 uComponentHandle)
{
	const Vsp::Component* pComponent = Vsp::Scene::Get().FindComponent(uComponentHandle);
	return (pComponent != nullptr && pComponent->IsEnabled()) ? 1 : 0;
}

CSHARP_EXPORT void VspComponent_SetEnabled(uint32 uComponentHandle, int32 bIsEnabled)
{
	Vsp::Component* pComponent = Vsp::Scene::Get().FindComponent(uComponentHandle);
	if (pComponent != nullptr)
	{
		pComponent->SetEnabled(bIsEnabled != 0);
	}
}

CSHARP_EXPORT int32 VspComponent_IsRenderable(uint32 uComponentHandle)
{
	const Vsp::Component* pComponent = Vsp::Scene::Get().FindComponent(uComponentHandle);
	return (pComponent != nullptr && pComponent->GetRenderState().bIsRenderable) ? 1 : 0;
}

CSHARP_EXPORT void VspComponent_SetRenderable(uint32 uComponentHandle, int32 bIsRenderable)
{
	Vsp::Component* pComponent = Vsp::Scene::Get().FindComponent(uComponentHandle);
	if (pComponent != nullptr)
	{
		pComponent->GetMutableRenderState().bIsRenderable = (bIsRenderable != 0);
	}
}

// -------- Frame iteration (the draw list source) --------

CSHARP_EXPORT uint32 VspComponent_GetRenderableCount()
{
	return Vsp::Scene::Get().GetRenderableComponentCount();
}

CSHARP_EXPORT uint32 VspComponent_GetRenderableHandle(uint32 uRenderableIndex)
{
	return Vsp::Scene::Get().GetRenderableComponentHandle(uRenderableIndex);
}

// -------- Scene diagnostics --------

CSHARP_EXPORT uint32 VspScene_GetLiveGameObjectCount()
{
	return Vsp::Scene::Get().GetLiveGameObjectCount();
}

CSHARP_EXPORT uint32 VspScene_GetLiveTransformCount()
{
	return Vsp::Scene::Get().GetLiveTransformCount();
}

CSHARP_EXPORT uint32 VspScene_GetLiveComponentCount()
{
	return Vsp::Scene::Get().GetLiveComponentCount();
}
