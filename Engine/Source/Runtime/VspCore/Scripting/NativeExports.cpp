#include "RuntimePCH.h"

#include <cstring>

#include "Classes/Time.h"
#include "Common/PlatformMisc.h"
#include "Core/Input/InputManager.h"
#include "Core/Logging/Log.h"
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

// -------- Cursor --------
// How the pointer behaves while the game is played. The engine owns the pointer's
// visibility and its freedom of movement; a game asks for the mode it wants.

CSHARP_EXPORT int32 VspInput_GetCursorMode()
{
	return static_cast<int32>(Vsp::InputManager::Get().GetCursorMode());
}

CSHARP_EXPORT void VspInput_SetCursorMode(int32 nCursorMode)
{
	// Anything the engine does not know is the ordinary pointer, which is the safe
	// answer: it is the mode that leaves the user in control of their machine.
	Vsp::InputManager::CursorMode eCursorMode = Vsp::InputManager::CursorMode::Visible;
	if (nCursorMode == static_cast<int32>(Vsp::InputManager::CursorMode::Confined))
	{
		eCursorMode = Vsp::InputManager::CursorMode::Confined;
	}
	else if (nCursorMode == static_cast<int32>(Vsp::InputManager::CursorMode::Locked))
	{
		eCursorMode = Vsp::InputManager::CursorMode::Locked;
	}

	Vsp::InputManager::Get().SetCursorMode(eCursorMode);
}

CSHARP_EXPORT int32 VspInput_IsCursorLocked()
{
	return Vsp::InputManager::Get().IsCursorLocked() ? 1 : 0;
}

// -------- Time --------
// The engine clock (Classes/Time) is the single source of frame timing; these
// exports are the whole surface managed code sees of it.

CSHARP_EXPORT float VspTime_GetDeltaTime()
{
	return Vsp::Time::Get().GetDeltaTime();
}

CSHARP_EXPORT float VspTime_GetUnscaledDeltaTime()
{
	return Vsp::Time::Get().GetUnscaledDeltaTime();
}

CSHARP_EXPORT float VspTime_GetElapsedTime()
{
	return Vsp::Time::Get().GetElapsedTime();
}

CSHARP_EXPORT float VspTime_GetUnscaledElapsedTime()
{
	return Vsp::Time::Get().GetUnscaledElapsedTime();
}

CSHARP_EXPORT int64 VspTime_GetFrameCount()
{
	return static_cast<int64>(Vsp::Time::Get().GetFrameCount());
}

CSHARP_EXPORT float VspTime_GetFramesPerSecond()
{
	return Vsp::Time::Get().GetFramesPerSecond();
}

CSHARP_EXPORT float VspTime_GetTimeScale()
{
	return Vsp::Time::Get().GetTimeScale();
}

CSHARP_EXPORT void VspTime_SetTimeScale(float fTimeScale)
{
	Vsp::Time::Get().SetTimeScale(fTimeScale);
}

CSHARP_EXPORT int32 VspTime_IsFixedTimeStep()
{
	return Vsp::Time::Get().IsFixedTimeStep() ? 1 : 0;
}

CSHARP_EXPORT float VspTime_GetFixedDeltaTime()
{
	return Vsp::Time::Get().GetFixedDeltaSeconds();
}

// -------- Engine paths --------

// Directory the executable lives in, as UTF-8. Managed code uses it to find the
// assets that are staged next to the executable - the compiled shaders above all.
CSHARP_EXPORT int32 VspPlatform_GetExecutableDirectoryUtf8(char* pBufferUtf8, int32 nBufferCapacityBytes)
{
	if (pBufferUtf8 == nullptr || nBufferCapacityBytes <= 0)
	{
		return 0;
	}

	const Vsp::VspString sExecutableDirectory = Vsp::PlatformMisc::GetExecutableDirectoryPath();
	const size_t nTextByteCount = strlen(sExecutableDirectory.GetData());
	const size_t nMaxCopyByteCount = static_cast<size_t>(nBufferCapacityBytes) - 1u;
	const size_t nCopyByteCount = nTextByteCount < nMaxCopyByteCount ? nTextByteCount : nMaxCopyByteCount;

	if (nCopyByteCount > 0)
	{
		memcpy(pBufferUtf8, sExecutableDirectory.GetData(), nCopyByteCount);
	}
	pBufferUtf8[nCopyByteCount] = '\0';
	return static_cast<int32>(nCopyByteCount);
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
