#pragma once

#include <cstdio>

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

		// -------- File system --------
		// True when the path addresses an existing file. Used for the startup
		// probes that locate the shipped .NET runtime.
		static bool DoesFileExist(const VspString& sFilePath);

		// Opens a file for BINARY READING, or returns nullptr (with a log line
		// when the path is unusable). The spelling of the call is the platform's
		// business - fopen_s is MSVC/Annex K only - so every reader in the engine
		// opens its file through here instead of naming one itself.
		static FILE* OpenFileForReading(const VspString& sFilePath);

		// -------- Dynamic library loading (DLL / shared object) --------
		static void* LoadDynamicLibrary(const VspString& sFilePath);
		static void UnloadDynamicLibrary(void* pLibraryHandle);
		static void* GetDynamicLibraryFunction(void* pLibraryHandle, const char* pFunctionName);

		// -------- Window message injection (acceptance tests) --------
		// Posts a synthetic key-down (bKeyDown = true) or key-up message into
		// the given native window handle.
		static void PostWindowKeyMessage(void* pWindowHandle, uint32 uVirtualKeyCode, bool bKeyDown);

		// Posts a synthetic mouse-move message that puts the pointer at the
		// given CLIENT coordinate. The engine's mouse delta follows from
		// successive positions, so posting a series of these moves a camera
		// exactly as a physical mouse does.
		static void PostWindowMouseMoveMessage(void* pWindowHandle, int32 nClientX, int32 nClientY);

		// -------- Cursor (the pointer a window shows) --------
		// Hides or shows the mouse pointer. A game hides it while the pointer
		// turns a camera and shows it again when a menu opens. Idempotent: asking
		// for a state it is already in changes nothing. Best effort on a platform
		// that has no such notion; never an error.
		static void SetCursorVisible(bool bIsVisible);

		// Keeps the pointer inside the window's client area, or lets it go again.
		// A confined pointer cannot leave the game to click something behind it.
		static void ConfineCursorToWindow(void* pWindowHandle);
		static void ReleaseCursorConfinement();

		// True while the given window is the one the user's input goes to. A window
		// in the background does not own the pointer, so the engine gives it back.
		static bool IsWindowFocused(void* pWindowHandle);

		// Puts the pointer at the centre of the window's client area and reports
		// where it landed, in client coordinates. Returns false when there is no
		// window, when it could not be moved, or when the window is not the one
		// the user is working in - a window in the background does not own the
		// pointer and must not move it.
		static bool CentreCursorInWindow(void* pWindowHandle, int32& outClientX, int32& outClientY);

		// -------- Debugger / user prompts --------
		static void WriteToDebugOutput(const char* pMessageUtf8);

		// Shows a modal error prompt (crash report). The process is expected
		// to terminate right after.
		static void ShowErrorPrompt(const char* pTitleUtf8, const char* pMessageUtf8);

		// -------- Process lifetime --------
		// Ends the current process immediately with the given exit code. Only
		// the fatal-error path (ProcessFailedExit) calls this: every other
		// failure returns an empty value instead of killing the engine.
		static void TerminateProcess(int32 nExitCode);

		// -------- Time --------
		// Milliseconds since the system started (used for log timestamps).
		static uint64 GetElapsedMilliseconds();

		// -------- Threads --------
		// Identifier of the CALLING thread, as the platform names it. The engine
		// uses it to say which thread created a service and to report a service
		// used from any other one (Core/EngineServices.h). Returns 0 when the
		// platform cannot name a thread, which the callers treat as "unknown" and
		// do not report on.
		static uint64 GetCurrentThreadId();
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
