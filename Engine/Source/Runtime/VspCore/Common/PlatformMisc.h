#pragma once

#include "Core/Core.h"
#include "Core/String/VspString.h"

namespace Vsp
{
	// -------------------------------------------------------------------------
	// PlatformMisc
	// -------------------------------------------------------------------------
	// Platform service facade. ALL Windows-only code is encapsulated here and
	// dispatched with #if VSP_PLATFORM_WINDOWS inside the implementation - the
	// rest of the engine never includes platform headers. Every function logs
	// its own errors through the Log module, reports failure through return
	// values and never throws.
	// -------------------------------------------------------------------------
	class RUNTIME_API PlatformMisc
	{
	public:
		// Returns the directory containing the current executable, without a
		// trailing separator. Empty when it cannot be determined.
		static VspString GetExecutableDirectoryPath();

		// The executable module's own handle (used e.g. for icon resources
		// and the Vulkan window surface).
		static void* GetProcessModuleHandle();

		// -------- Process environment variables --------
		static void SetEnvironmentVariableValue(const VspString& sName, const VspString& sValue);

		// Reads a narrow environment variable. Returns the number of
		// characters copied (excluding the null terminator); 0 means the
		// variable is unset or empty.
		static uint32 GetEnvironmentVariableValue(const char* pName, char* pBuffer, uint32 uBufferSize);

		// -------- Dynamic library loading (DLL / shared object) --------
		static void* LoadDynamicLibrary(const VspString& sFilePath);
		static void UnloadDynamicLibrary(void* pLibraryHandle);
		static void* GetDynamicLibraryFunction(void* pLibraryHandle, const char* pFunctionName);

		// -------- Window message injection (acceptance tests) --------
		// Posts a synthetic key-down (bKeyDown = true) or key-up message into
		// the given native window handle.
		static void PostWindowKeyMessage(void* pWindowHandle, uint32 uVirtualKeyCode, bool bKeyDown);

		// -------- Debugger / user prompts --------
		static void WriteToDebugOutput(const char* pMessageUtf8);

		// Shows a modal error prompt (crash report). The process is expected
		// to terminate right after.
		static void ShowErrorPrompt(const char* pTitleUtf8, const char* pMessageUtf8);

		// -------- Time --------
		// Milliseconds since the system started (used for log timestamps).
		static uint64 GetElapsedMilliseconds();
	};

	// -------------------------------------------------------------------------
	// HighResolutionTimer
	// -------------------------------------------------------------------------
	// Frame timer based on the platform's highest-resolution clock. Tick()
	// returns the seconds since the previous tick (0.0 on the first call) and
	// clamps huge jumps (long stalls / debugging breaks) to
	// k_fMaximumDeltaSeconds so simulation never explodes.
	// -------------------------------------------------------------------------
	class RUNTIME_API HighResolutionTimer
	{
	public:
		static constexpr float k_fMaximumDeltaSeconds = 0.1f;

		float Tick();

	private:
		int64 m_nTimerFrequency = 0;
		int64 m_nLastTickCounter = 0;
		bool m_bHasLastTick = false;
	};
}
