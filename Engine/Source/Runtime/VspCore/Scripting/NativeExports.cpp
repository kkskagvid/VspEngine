#include "RuntimePCH.h"

#include "Core/Input/InputManager.h"
#include "Core/Logging/Log.h"
#include "Graphics/RenderCore.h"
#include "Scripting/ScriptCore.h"
#include "Scripting/ScriptExport.h"

// -------------------------------------------------------------------------
// Native exports consumed by managed code (C# -> C++ direction).
// VspEngine's NativeApi.cs P/Invokes these exact names from VspCore.dll.
// Everything is plain data in/out — no exceptions cross the boundary.
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

// -------- Transform --------

CSHARP_EXPORT void VspTransform_SetPosition(uint32 uInstanceId, float fPositionX, float fPositionY)
{
	Vsp::ScriptCore::Get().SetTransformPosition(uInstanceId, fPositionX, fPositionY);
}

CSHARP_EXPORT float VspTransform_GetPositionX(uint32 uInstanceId)
{
	float fPositionX = 0.0f;
	float fPositionY = 0.0f;
	Vsp::ScriptCore::Get().GetTransformPosition(uInstanceId, fPositionX, fPositionY);
	return fPositionX;
}

CSHARP_EXPORT float VspTransform_GetPositionY(uint32 uInstanceId)
{
	float fPositionX = 0.0f;
	float fPositionY = 0.0f;
	Vsp::ScriptCore::Get().GetTransformPosition(uInstanceId, fPositionX, fPositionY);
	return fPositionY;
}

// -------- Renderer --------

CSHARP_EXPORT void VspRenderer_SetColorMode(uint32 uInstanceId, int32 nColorMode)
{
	Vsp::ScriptCore::Get().SetColorMode(uInstanceId, nColorMode);
}

CSHARP_EXPORT int32 VspRenderer_GetColorMode(uint32 uInstanceId)
{
	return Vsp::ScriptCore::Get().GetColorMode(uInstanceId);
}

// -------- Render flow (driven by VspEngine.Rendering.RenderFlow) --------
// The managed render flow implements the frame (BeginFrame -> clear ->
// draws -> EndFrame) on top of these commands; the native Vulkan renderer
// consumes the collected commands when it records the command buffer.

CSHARP_EXPORT void VspRenderer_BeginFrame()
{
	Vsp::RenderCore::Get().BeginFrame();
}

CSHARP_EXPORT void VspRenderer_SetClearColor(float fColorR, float fColorG, float fColorB, float fColorA)
{
	Vsp::RenderCore::Get().SetClearColor(fColorR, fColorG, fColorB, fColorA);
}

CSHARP_EXPORT void VspRenderer_DrawTriangle(float fPositionX, float fPositionY, int32 nColorMode)
{
	Vsp::RenderCore::Get().DrawTriangle(fPositionX, fPositionY, nColorMode);
}

CSHARP_EXPORT void VspRenderer_EndFrame()
{
	Vsp::RenderCore::Get().EndFrame();
}

// -------- Logging --------

CSHARP_EXPORT void VspLog_Message(const char* pMessageUtf8)
{
	if (pMessageUtf8 != nullptr)
	{
		LOG_INFO("Script", "{}", pMessageUtf8);
	}
}
