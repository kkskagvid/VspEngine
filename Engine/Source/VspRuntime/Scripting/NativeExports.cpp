#include "RuntimePCH.h"

#include "Core/Input/InputManager.h"
#include "Core/Logging/Log.h"
#include "Scripting/ScriptCore.h"
#include "Scripting/ScriptExport.h"

// -------------------------------------------------------------------------
// Native exports consumed by managed code (C# -> C++ direction).
// VspPlayer's NativeApi.cs P/Invokes these exact names from VspRuntime.dll.
// Everything is plain data in/out — no exceptions cross the boundary.
// -------------------------------------------------------------------------

// -------- Input (keyboard) --------

CSHARP_EXPORT int32_t VspInput_IsKeyDown(int32_t nKeyCode)
{
	return Vsp::InputManager::Get().IsKeyDown(static_cast<Vsp::KeyCode>(nKeyCode)) ? 1 : 0;
}

CSHARP_EXPORT int32_t VspInput_WasKeyPressed(int32_t nKeyCode)
{
	return Vsp::InputManager::Get().WasKeyPressed(static_cast<Vsp::KeyCode>(nKeyCode)) ? 1 : 0;
}

CSHARP_EXPORT int32_t VspInput_WasKeyReleased(int32_t nKeyCode)
{
	return Vsp::InputManager::Get().WasKeyReleased(static_cast<Vsp::KeyCode>(nKeyCode)) ? 1 : 0;
}

// -------- Input (mouse) --------

CSHARP_EXPORT int32_t VspInput_IsMouseButtonDown(int32_t nButton)
{
	return Vsp::InputManager::Get().IsMouseButtonDown(nButton) ? 1 : 0;
}

CSHARP_EXPORT int32_t VspInput_WasMouseButtonPressed(int32_t nButton)
{
	return Vsp::InputManager::Get().WasMouseButtonPressed(nButton) ? 1 : 0;
}

CSHARP_EXPORT int32_t VspInput_WasMouseButtonReleased(int32_t nButton)
{
	return Vsp::InputManager::Get().WasMouseButtonReleased(nButton) ? 1 : 0;
}

CSHARP_EXPORT float VspInput_GetMousePositionX()
{
	return Vsp::InputManager::Get().GetMousePositionX();
}

CSHARP_EXPORT float VspInput_GetMousePositionY()
{
	return Vsp::InputManager::Get().GetMousePositionY();
}

CSHARP_EXPORT float VspInput_GetMouseDeltaX()
{
	return Vsp::InputManager::Get().GetMouseDeltaX();
}

CSHARP_EXPORT float VspInput_GetMouseDeltaY()
{
	return Vsp::InputManager::Get().GetMouseDeltaY();
}

CSHARP_EXPORT float VspInput_GetScrollX()
{
	return Vsp::InputManager::Get().GetScrollX();
}

CSHARP_EXPORT float VspInput_GetScrollY()
{
	return Vsp::InputManager::Get().GetScrollY();
}

// -------- Time --------

CSHARP_EXPORT float VspTime_GetDeltaTime()
{
	return Vsp::ScriptCore::Get().GetDeltaTime();
}

CSHARP_EXPORT float VspTime_GetElapsedTime()
{
	return Vsp::ScriptCore::Get().GetElapsedTime();
}

// -------- Transform --------

CSHARP_EXPORT void VspTransform_SetPosition(uint32_t uInstanceId, float fPositionX, float fPositionY)
{
	Vsp::ScriptCore::Get().SetTransformPosition(uInstanceId, fPositionX, fPositionY);
}

CSHARP_EXPORT float VspTransform_GetPositionX(uint32_t uInstanceId)
{
	float fPositionX = 0.0f;
	float fPositionY = 0.0f;
	Vsp::ScriptCore::Get().GetTransformPosition(uInstanceId, fPositionX, fPositionY);
	return fPositionX;
}

CSHARP_EXPORT float VspTransform_GetPositionY(uint32_t uInstanceId)
{
	float fPositionX = 0.0f;
	float fPositionY = 0.0f;
	Vsp::ScriptCore::Get().GetTransformPosition(uInstanceId, fPositionX, fPositionY);
	return fPositionY;
}

// -------- Renderer --------

CSHARP_EXPORT void VspRenderer_SetColorMode(uint32_t uInstanceId, int32_t nColorMode)
{
	Vsp::ScriptCore::Get().SetColorMode(uInstanceId, nColorMode);
}

CSHARP_EXPORT int32_t VspRenderer_GetColorMode(uint32_t uInstanceId)
{
	return Vsp::ScriptCore::Get().GetColorMode(uInstanceId);
}

// -------- Logging --------

CSHARP_EXPORT void VspLog_Message(const char* pMessageUtf8)
{
	if (pMessageUtf8 != nullptr)
	{
		LOG_INFO("Script", "{}", pMessageUtf8);
	}
}
