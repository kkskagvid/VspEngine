#include "RuntimePCH.h"

#include "Core/Input/InputManager.h"
#include "Core/Logging/Log.h"
#include "Scripting/ScriptCore.h"
#include "Scripting/ScriptExport.h"

// -------------------------------------------------------------------------
// Native exports consumed by managed code (C# -> C++ direction).
// VspEngine's NativeApi.cs P/Invokes these exact names from VspCore.dll.
// This file covers input, time and logging; the scene object model lives in
// SceneExports.cpp and the wrapped graphics API in RhiExports.cpp.
// Everything is plain data in/out - no exceptions cross the boundary.
// -------------------------------------------------------------------------

// -------- Input (keyboard) --------

CSHARP_EXPORT int32 VspInput_IsKeyDown(int32 nKeyCode)
{
	return Vsp::InputManager::Get().IsKeyDown(static_cast<Vsp::KeyCode>(nKeyCode)) ? 1 : 0;
}

CSHARP_EXPORT int32 VspInput_WasKeyPressed(int32 nKeyCode)
{
	return Vsp::InputManager::Get().WasKeyPressed(static_cast<Vsp::KeyCode>(nKeyCode)) ? 1 : 0;
}

CSHARP_EXPORT int32 VspInput_WasKeyReleased(int32 nKeyCode)
{
	return Vsp::InputManager::Get().WasKeyReleased(static_cast<Vsp::KeyCode>(nKeyCode)) ? 1 : 0;
}

// -------- Input (mouse) --------

CSHARP_EXPORT int32 VspInput_IsMouseButtonDown(int32 nButton)
{
	return Vsp::InputManager::Get().IsMouseButtonDown(nButton) ? 1 : 0;
}

CSHARP_EXPORT int32 VspInput_WasMouseButtonPressed(int32 nButton)
{
	return Vsp::InputManager::Get().WasMouseButtonPressed(nButton) ? 1 : 0;
}

CSHARP_EXPORT int32 VspInput_WasMouseButtonReleased(int32 nButton)
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

// -------- Logging --------

CSHARP_EXPORT void VspLog_Message(const char* pMessageUtf8)
{
	if (pMessageUtf8 != nullptr)
	{
		LOG_INFO("Script", "{}", pMessageUtf8);
	}
}

CSHARP_EXPORT void VspLog_Debug(const char* pMessageUtf8)
{
	if (pMessageUtf8 != nullptr)
	{
		LOG_DEBUG("Script", "{}", pMessageUtf8);
	}
}

CSHARP_EXPORT void VspLog_Info(const char* pMessageUtf8)
{
	if (pMessageUtf8 != nullptr)
	{
		LOG_INFO("Script", "{}", pMessageUtf8);
	}
}

CSHARP_EXPORT void VspLog_Warning(const char* pMessageUtf8)
{
	if (pMessageUtf8 != nullptr)
	{
		LOG_WARNING("Script", "{}", pMessageUtf8);
	}
}

CSHARP_EXPORT void VspLog_Error(const char* pMessageUtf8)
{
	if (pMessageUtf8 != nullptr)
	{
		LOG_ERROR("Script", "{}", pMessageUtf8);
	}
}
