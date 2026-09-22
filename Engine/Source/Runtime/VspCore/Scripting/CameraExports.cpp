#include "RuntimePCH.h"

#include "Classes/Camera.h"
#include "Classes/GameObject.h"
#include "Classes/Scene.h"
#include "Scripting/ScriptExport.h"

// -------------------------------------------------------------------------
// Camera exports consumed by managed code (C# -> C++).
// VspEngine.Camera P/Invokes these exact names from VspCore.dll.
//
// A camera belongs to a game object (its transform is where the camera is and
// which way it looks); the projection settings and the matrices that follow
// from them live in Classes/Camera. Everything is plain data in/out - no
// exceptions cross the boundary.
// -------------------------------------------------------------------------

CSHARP_EXPORT uint32 VspCamera_Create(uint32 uGameObjectHandle)
{
	return Vsp::Scene::Get().CreateCamera(uGameObjectHandle);
}

CSHARP_EXPORT int32 VspCamera_Destroy(uint32 uCameraHandle)
{
	return Vsp::Scene::Get().DestroyCamera(uCameraHandle) ? 1 : 0;
}

CSHARP_EXPORT uint32 VspCamera_GetGameObject(uint32 uCameraHandle)
{
	const Vsp::Camera* pCamera = Vsp::Scene::Get().FindCamera(uCameraHandle);
	return pCamera != nullptr ? pCamera->GetOwnerGameObjectHandle() : Vsp::k_nInvalidObjectHandle;
}

CSHARP_EXPORT int32 VspCamera_GetProjectionMode(uint32 uCameraHandle)
{
	const Vsp::Camera* pCamera = Vsp::Scene::Get().FindCamera(uCameraHandle);
	return pCamera != nullptr ? static_cast<int32>(pCamera->GetProjectionMode()) : 0;
}

CSHARP_EXPORT void VspCamera_SetProjectionMode(uint32 uCameraHandle, int32 nProjectionMode)
{
	Vsp::Camera* pCamera = Vsp::Scene::Get().FindCamera(uCameraHandle);
	if (pCamera != nullptr)
	{
		pCamera->SetProjectionMode(static_cast<Vsp::CameraProjectionMode>(nProjectionMode));
	}
}

CSHARP_EXPORT float VspCamera_GetFieldOfView(uint32 uCameraHandle)
{
	const Vsp::Camera* pCamera = Vsp::Scene::Get().FindCamera(uCameraHandle);
	return pCamera != nullptr ? pCamera->GetFieldOfView() : 0.0f;
}

CSHARP_EXPORT void VspCamera_SetFieldOfView(uint32 uCameraHandle, float fFieldOfView)
{
	Vsp::Camera* pCamera = Vsp::Scene::Get().FindCamera(uCameraHandle);
	if (pCamera != nullptr)
	{
		pCamera->SetFieldOfView(fFieldOfView);
	}
}

CSHARP_EXPORT float VspCamera_GetEffectiveFieldOfView(uint32 uCameraHandle)
{
	const Vsp::Camera* pCamera = Vsp::Scene::Get().FindCamera(uCameraHandle);
	return pCamera != nullptr ? pCamera->GetEffectiveFieldOfView() : 0.0f;
}

CSHARP_EXPORT float VspCamera_GetOrthographicSize(uint32 uCameraHandle)
{
	const Vsp::Camera* pCamera = Vsp::Scene::Get().FindCamera(uCameraHandle);
	return pCamera != nullptr ? pCamera->GetOrthographicSize() : 0.0f;
}

CSHARP_EXPORT void VspCamera_SetOrthographicSize(uint32 uCameraHandle, float fOrthographicSize)
{
	Vsp::Camera* pCamera = Vsp::Scene::Get().FindCamera(uCameraHandle);
	if (pCamera != nullptr)
	{
		pCamera->SetOrthographicSize(fOrthographicSize);
	}
}

CSHARP_EXPORT float VspCamera_GetNearClipPlane(uint32 uCameraHandle)
{
	const Vsp::Camera* pCamera = Vsp::Scene::Get().FindCamera(uCameraHandle);
	return pCamera != nullptr ? pCamera->GetNearClipPlane() : 0.0f;
}

CSHARP_EXPORT void VspCamera_SetNearClipPlane(uint32 uCameraHandle, float fNearClipPlane)
{
	Vsp::Camera* pCamera = Vsp::Scene::Get().FindCamera(uCameraHandle);
	if (pCamera != nullptr)
	{
		pCamera->SetNearClipPlane(fNearClipPlane);
	}
}

CSHARP_EXPORT float VspCamera_GetFarClipPlane(uint32 uCameraHandle)
{
	const Vsp::Camera* pCamera = Vsp::Scene::Get().FindCamera(uCameraHandle);
	return pCamera != nullptr ? pCamera->GetFarClipPlane() : 0.0f;
}

CSHARP_EXPORT void VspCamera_SetFarClipPlane(uint32 uCameraHandle, float fFarClipPlane)
{
	Vsp::Camera* pCamera = Vsp::Scene::Get().FindCamera(uCameraHandle);
	if (pCamera != nullptr)
	{
		pCamera->SetFarClipPlane(fFarClipPlane);
	}
}

CSHARP_EXPORT float VspCamera_GetAspect(uint32 uCameraHandle)
{
	const Vsp::Camera* pCamera = Vsp::Scene::Get().FindCamera(uCameraHandle);
	return pCamera != nullptr ? pCamera->GetAspect() : 0.0f;
}

CSHARP_EXPORT void VspCamera_SetAspect(uint32 uCameraHandle, float fAspect)
{
	Vsp::Camera* pCamera = Vsp::Scene::Get().FindCamera(uCameraHandle);
	if (pCamera != nullptr)
	{
		pCamera->SetAspect(fAspect);
	}
}

CSHARP_EXPORT float VspCamera_GetFocalLength(uint32 uCameraHandle)
{
	const Vsp::Camera* pCamera = Vsp::Scene::Get().FindCamera(uCameraHandle);
	return pCamera != nullptr ? pCamera->GetFocalLength() : 0.0f;
}

CSHARP_EXPORT void VspCamera_SetFocalLength(uint32 uCameraHandle, float fFocalLength)
{
	Vsp::Camera* pCamera = Vsp::Scene::Get().FindCamera(uCameraHandle);
	if (pCamera != nullptr)
	{
		pCamera->SetFocalLength(fFocalLength);
	}
}

CSHARP_EXPORT float VspCamera_GetSensorWidth(uint32 uCameraHandle)
{
	const Vsp::Camera* pCamera = Vsp::Scene::Get().FindCamera(uCameraHandle);
	return pCamera != nullptr ? pCamera->GetSensorWidth() : 0.0f;
}

CSHARP_EXPORT void VspCamera_SetSensorWidth(uint32 uCameraHandle, float fSensorWidth)
{
	Vsp::Camera* pCamera = Vsp::Scene::Get().FindCamera(uCameraHandle);
	if (pCamera != nullptr)
	{
		pCamera->SetSensorWidth(fSensorWidth);
	}
}

CSHARP_EXPORT float VspCamera_GetSensorHeight(uint32 uCameraHandle)
{
	const Vsp::Camera* pCamera = Vsp::Scene::Get().FindCamera(uCameraHandle);
	return pCamera != nullptr ? pCamera->GetSensorHeight() : 0.0f;
}

CSHARP_EXPORT void VspCamera_SetSensorHeight(uint32 uCameraHandle, float fSensorHeight)
{
	Vsp::Camera* pCamera = Vsp::Scene::Get().FindCamera(uCameraHandle);
	if (pCamera != nullptr)
	{
		pCamera->SetSensorHeight(fSensorHeight);
	}
}

CSHARP_EXPORT float VspCamera_GetAperture(uint32 uCameraHandle)
{
	const Vsp::Camera* pCamera = Vsp::Scene::Get().FindCamera(uCameraHandle);
	return pCamera != nullptr ? pCamera->GetAperture() : 0.0f;
}

CSHARP_EXPORT void VspCamera_SetAperture(uint32 uCameraHandle, float fAperture)
{
	Vsp::Camera* pCamera = Vsp::Scene::Get().FindCamera(uCameraHandle);
	if (pCamera != nullptr)
	{
		pCamera->SetAperture(fAperture);
	}
}

CSHARP_EXPORT float VspCamera_GetFocusDistance(uint32 uCameraHandle)
{
	const Vsp::Camera* pCamera = Vsp::Scene::Get().FindCamera(uCameraHandle);
	return pCamera != nullptr ? pCamera->GetFocusDistance() : 0.0f;
}

CSHARP_EXPORT void VspCamera_SetFocusDistance(uint32 uCameraHandle, float fFocusDistance)
{
	Vsp::Camera* pCamera = Vsp::Scene::Get().FindCamera(uCameraHandle);
	if (pCamera != nullptr)
	{
		pCamera->SetFocusDistance(fFocusDistance);
	}
}

// -------- Matrices (column-major, 16 floats each) --------

CSHARP_EXPORT void VspCamera_GetViewMatrix(uint32 uCameraHandle, float* pOutMatrix16)
{
	if (pOutMatrix16 == nullptr)
	{
		return;
	}

	const Vsp::Camera* pCamera = Vsp::Scene::Get().FindCamera(uCameraHandle);
	if (pCamera != nullptr)
	{
		pCamera->GetViewMatrix(pOutMatrix16);
	}
}

CSHARP_EXPORT void VspCamera_GetProjectionMatrix(uint32 uCameraHandle, float* pOutMatrix16)
{
	if (pOutMatrix16 == nullptr)
	{
		return;
	}

	const Vsp::Camera* pCamera = Vsp::Scene::Get().FindCamera(uCameraHandle);
	if (pCamera != nullptr)
	{
		pCamera->GetProjectionMatrix(pOutMatrix16);
	}
}

CSHARP_EXPORT void VspCamera_GetViewProjectionMatrix(uint32 uCameraHandle, float* pOutMatrix16)
{
	if (pOutMatrix16 == nullptr)
	{
		return;
	}

	const Vsp::Camera* pCamera = Vsp::Scene::Get().FindCamera(uCameraHandle);
	if (pCamera != nullptr)
	{
		pCamera->GetViewProjectionMatrix(pOutMatrix16);
	}
}

CSHARP_EXPORT void VspCamera_GetPosition(uint32 uCameraHandle, float* pOutXyz)
{
	if (pOutXyz == nullptr)
	{
		return;
	}

	const Vsp::Camera* pCamera = Vsp::Scene::Get().FindCamera(uCameraHandle);
	if (pCamera == nullptr)
	{
		pOutXyz[0] = 0.0f;
		pOutXyz[1] = 0.0f;
		pOutXyz[2] = 0.0f;
		return;
	}
	pCamera->GetWorldPosition(pOutXyz[0], pOutXyz[1], pOutXyz[2]);
}

CSHARP_EXPORT uint32 VspScene_GetCameraHandle(uint32 uCameraIndex)
{
	return Vsp::Scene::Get().GetLiveCameraHandle(uCameraIndex);
}

CSHARP_EXPORT uint32 VspScene_GetLiveCameraCount()
{
	return Vsp::Scene::Get().GetLiveCameraCount();
}
