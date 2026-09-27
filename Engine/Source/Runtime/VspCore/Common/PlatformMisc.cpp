#include "RuntimePCH.h"

#include <chrono>
#include <cstdlib>

#include "Core/Logging/Log.h"
#include "Common/PlatformMisc.h"

#if VSP_PLATFORM_WINDOWS
	#define WIN32_LEAN_AND_MEAN
	#include <Windows.h>
	#include <shellapi.h>
#endif

namespace Vsp
{
	static constexpr const char* kLogTag = "PlatformMisc";

	// -------------------------------------------------------------------------
	// Executable / module handles
	// -------------------------------------------------------------------------

	VspString PlatformMisc::GetExecutableDirectoryPath()
	{
#if VSP_PLATFORM_WINDOWS
		wchar_t sExecutablePathBuffer[2048] = {};
		const DWORD nLength = GetModuleFileNameW(nullptr, sExecutablePathBuffer, 2048);
		if (nLength == 0)
		{
			LOG_ERROR(kLogTag, "GetModuleFileNameW failed.");
			return VspString();
		}

		VspString sExecutablePath(sExecutablePathBuffer);

		// Search backwards for the final path separator.
		for (size_t nIndex = sExecutablePath.GetByteLength(); nIndex > 0; --nIndex)
		{
			const char cCharacter = sExecutablePath.GetData()[nIndex - 1];
			if (cCharacter == '\\' || cCharacter == '/')
			{
				return sExecutablePath.GetSubString(0, nIndex - 1);
			}
		}
		return sExecutablePath;
#else
		LOG_ERROR(kLogTag, "GetExecutableDirectoryPath is not implemented on this platform.");
		return VspString();
#endif
	}

	void* PlatformMisc::GetProcessModuleHandle()
	{
#if VSP_PLATFORM_WINDOWS
		return static_cast<void*>(::GetModuleHandleW(nullptr));
#else
		return nullptr;
#endif
	}

	// -------------------------------------------------------------------------
	// Environment variables
	// -------------------------------------------------------------------------

	void PlatformMisc::SetEnvironmentVariableValue(const VspString& sName, const VspString& sValue)
	{
#if VSP_PLATFORM_WINDOWS
		::SetEnvironmentVariableW(sName.ToWideText().GetData(), sValue.ToWideText().GetData());
#else
		LOG_ERROR(kLogTag, "SetEnvironmentVariableValue is not implemented on this platform.");
#endif
	}

	uint32 PlatformMisc::GetEnvironmentVariableValue(const char* pName, char* pBuffer, uint32 uBufferSize)
	{
		if (pName == nullptr || pBuffer == nullptr || uBufferSize == 0)
		{
			return 0;
		}

#if VSP_PLATFORM_WINDOWS
		return ::GetEnvironmentVariableA(pName, pBuffer, uBufferSize);
#else
		pBuffer[0] = '\0';
		return 0;
#endif
	}

	// -------------------------------------------------------------------------
	// File system
	// -------------------------------------------------------------------------

	FILE* PlatformMisc::OpenFileForReading(const VspString& sFilePath)
	{
		if (sFilePath.IsEmpty())
		{
			LOG_ERROR(kLogTag, "OpenFileForReading was given an empty path.");
			return nullptr;
		}

#if defined(_MSC_VER)
		// fopen_s reports through its return value, which is what the engine's
		// no-exception rule wants; the standard call is the only one elsewhere.
		FILE* pFile = nullptr;
		if (fopen_s(&pFile, sFilePath.GetData(), "rb") != 0)
		{
			return nullptr;
		}
		return pFile;
#else
		return fopen(sFilePath.GetData(), "rb");
#endif
	}

	bool PlatformMisc::DoesFileExist(const VspString& sFilePath)
	{
		if (sFilePath.IsEmpty())
		{
			return false;
		}

#if VSP_PLATFORM_WINDOWS
		const DWORD nAttributes = ::GetFileAttributesW(sFilePath.ToWideText().GetData());
		return nAttributes != INVALID_FILE_ATTRIBUTES && (nAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
#else
		LOG_ERROR(kLogTag, "DoesFileExist is not implemented on this platform.");
		return false;
#endif
	}

	// -------------------------------------------------------------------------
	// Dynamic libraries
	// -------------------------------------------------------------------------

	void* PlatformMisc::LoadDynamicLibrary(const VspString& sFilePath)
	{
#if VSP_PLATFORM_WINDOWS
		void* pLibraryHandle = static_cast<void*>(::LoadLibraryW(sFilePath.ToWideText().GetData()));
		if (pLibraryHandle == nullptr)
		{
			LOG_ERROR(kLogTag, "LoadLibraryW failed for '{}'.", sFilePath.GetData());
		}
		return pLibraryHandle;
#else
		LOG_ERROR(kLogTag, "LoadDynamicLibrary is not implemented on this platform.");
		return nullptr;
#endif
	}

	void PlatformMisc::UnloadDynamicLibrary(void* pLibraryHandle)
	{
		if (pLibraryHandle == nullptr)
		{
			return;
		}

#if VSP_PLATFORM_WINDOWS
		::FreeLibrary(static_cast<HMODULE>(pLibraryHandle));
#endif
	}

	void* PlatformMisc::GetDynamicLibraryFunction(void* pLibraryHandle, const char* pFunctionName)
	{
		if (pLibraryHandle == nullptr || pFunctionName == nullptr)
		{
			return nullptr;
		}

#if VSP_PLATFORM_WINDOWS
		return reinterpret_cast<void*>(
			::GetProcAddress(static_cast<HMODULE>(pLibraryHandle), pFunctionName));
#else
		return nullptr;
#endif
	}

	// -------------------------------------------------------------------------
	// Window message injection
	// -------------------------------------------------------------------------

	void PlatformMisc::PostWindowKeyMessage(void* pWindowHandle, uint32 uVirtualKeyCode, bool bKeyDown)
	{
		if (pWindowHandle == nullptr)
		{
			return;
		}

#if VSP_PLATFORM_WINDOWS
		const UINT uMessage = bKeyDown ? WM_KEYDOWN : WM_KEYUP;
		::PostMessageW(
			static_cast<HWND>(pWindowHandle),
			uMessage,
			static_cast<WPARAM>(uVirtualKeyCode),
			0);
#else
		LOG_ERROR(kLogTag, "PostWindowKeyMessage is not implemented on this platform.");
#endif
	}

	void PlatformMisc::PostWindowMouseMoveMessage(void* pWindowHandle, int32 nClientX, int32 nClientY)
	{
		if (pWindowHandle == nullptr)
		{
			return;
		}

#if VSP_PLATFORM_WINDOWS
		// lParam carries the client coordinate as two 16-bit halves, which is
		// what GET_X_LPARAM/GET_Y_LPARAM read back.
		const LPARAM nPackedCoordinate =
			MAKELPARAM(static_cast<WORD>(static_cast<SHORT>(nClientX)),
				static_cast<WORD>(static_cast<SHORT>(nClientY)));
		::PostMessageW(static_cast<HWND>(pWindowHandle), WM_MOUSEMOVE, 0, nPackedCoordinate);
#else
		LOG_ERROR(kLogTag, "PostWindowMouseMoveMessage is not implemented on this platform.");
#endif
	}

	// -------------------------------------------------------------------------
	// Cursor
	// -------------------------------------------------------------------------
	// The pointer belongs to the window the user is working in, so every call here
	// is best effort: a window in the background, or a platform without the
	// notion, simply leaves the pointer alone.

	void PlatformMisc::SetCursorVisible(bool bIsVisible)
	{
#if VSP_PLATFORM_WINDOWS
		if (bIsVisible)
		{
			// The display counter is a COUNT, not a flag: it is raised until the
			// cursor is shown again, whatever hid it before, and the shape is put
			// back so the next frame draws the familiar arrow.
			while (::ShowCursor(TRUE) < 0)
			{
			}
			::SetCursor(::LoadCursorW(nullptr, IDC_ARROW));
		}
		else
		{
			// ... and lowered until it is gone, with the shape cleared as well so
			// it disappears on THIS frame rather than on the next WM_SETCURSOR.
			while (::ShowCursor(FALSE) >= 0)
			{
			}
			::SetCursor(nullptr);
		}
#else
		(void)bIsVisible;
#endif
	}

	bool PlatformMisc::IsWindowFocused(void* pWindowHandle)
	{
#if VSP_PLATFORM_WINDOWS
		if (pWindowHandle == nullptr)
		{
			return false;
		}

		// "The player is in the game" means the keyboard goes to THIS window: it is
		// the active window of this thread and it (or one of its children) holds the
		// focus. GetForegroundWindow would answer a different question - another
		// process' window that merely sits in front would pass it - and the pointer
		// belongs to whoever the user is typing to, not to whoever is on top.
		const HWND hWindow = static_cast<HWND>(pWindowHandle);
		if (::GetActiveWindow() != hWindow)
		{
			return false;
		}

		const HWND hFocusedWindow = ::GetFocus();
		return (hFocusedWindow == hWindow) || (::IsChild(hWindow, hFocusedWindow) != FALSE);
#else
		(void)pWindowHandle;
		return false;
#endif
	}

	void PlatformMisc::ConfineCursorToWindow(void* pWindowHandle)
	{
#if VSP_PLATFORM_WINDOWS
		if (pWindowHandle == nullptr)
		{
			return;
		}

		// ClipCursor works in SCREEN coordinates, so the client rectangle is
		// mapped through ClientToScreen first - including the window's borders.
		const HWND hWindow = static_cast<HWND>(pWindowHandle);
		RECT clientRectangle = {};
		if (!::GetClientRect(hWindow, &clientRectangle))
		{
			return;
		}

		POINT topLeft = { clientRectangle.left, clientRectangle.top };
		POINT bottomRight = { clientRectangle.right, clientRectangle.bottom };
		if (!::ClientToScreen(hWindow, &topLeft) || !::ClientToScreen(hWindow, &bottomRight))
		{
			return;
		}

		const RECT screenRectangle = { topLeft.x, topLeft.y, bottomRight.x, bottomRight.y };
		::ClipCursor(&screenRectangle);
#else
		(void)pWindowHandle;
#endif
	}

	void PlatformMisc::ReleaseCursorConfinement()
	{
#if VSP_PLATFORM_WINDOWS
		// The confinement is per-process in Win32 and outlives the window it was
		// asked for, so it is released explicitly rather than by closing anything.
		::ClipCursor(nullptr);
#endif
	}

	bool PlatformMisc::CentreCursorInWindow(void* pWindowHandle, int32& outClientX, int32& outClientY)
	{
        outClientX = 0;
        outClientY = 0;

#if VSP_PLATFORM_WINDOWS
        if (pWindowHandle == nullptr)
        {
            return false;
        }

        const HWND hWindow = static_cast<HWND>(pWindowHandle);

        // Only the window the user is working in may move the pointer: taking it
        // away from whatever they are really doing would be a bug, not a feature.
        if (::GetForegroundWindow() != hWindow)
        {
            return false;
        }

        RECT clientRectangle = {};
        if (!::GetClientRect(hWindow, &clientRectangle))
        {
            return false;
        }

        const int32 nClientX = (clientRectangle.right - clientRectangle.left) / 2;
        const int32 nClientY = (clientRectangle.bottom - clientRectangle.top) / 2;
        POINT screenPoint = { nClientX, nClientY };
        if (!::ClientToScreen(hWindow, &screenPoint))
        {
            return false;
        }

        if (!::SetCursorPos(screenPoint.x, screenPoint.y))
        {
            return false;
        }

        POINT actualPoint = {};
        if (::GetCursorPos(&actualPoint) && ::ScreenToClient(hWindow, &actualPoint))
        {
            outClientX = static_cast<int32>(actualPoint.x);
            outClientY = static_cast<int32>(actualPoint.y);
        }
        else
        {
            // Fall back to the requested centre if the read-back fails; the
            // recentre still works, the caller just tracks the intent instead
            // of the outcome.
            outClientX = nClientX;
            outClientY = nClientY;
        }

        return true;
#else
        (void)pWindowHandle;
        return false;
#endif
	}

	// -------------------------------------------------------------------------
	// Debugger / user prompts
	// -------------------------------------------------------------------------

	void PlatformMisc::WriteToDebugOutput(const char* pMessageUtf8)
	{
		if (pMessageUtf8 == nullptr)
		{
			return;
		}

#if VSP_PLATFORM_WINDOWS
		::OutputDebugStringA(pMessageUtf8);
#endif
	}

	void PlatformMisc::ShowErrorPrompt(const char* pTitleUtf8, const char* pMessageUtf8)
	{
#if VSP_PLATFORM_WINDOWS
		::MessageBoxA(
			nullptr,
			pMessageUtf8 != nullptr ? pMessageUtf8 : "",
			pTitleUtf8 != nullptr ? pTitleUtf8 : "Error",
			MB_OK | MB_ICONERROR);
#else
		LOG_ERROR(kLogTag, "{}: {}", pTitleUtf8 != nullptr ? pTitleUtf8 : "Error",
			pMessageUtf8 != nullptr ? pMessageUtf8 : "");
#endif
	}

	// -------------------------------------------------------------------------
	// Process lifetime
	// -------------------------------------------------------------------------

	void PlatformMisc::TerminateProcess(int32 nExitCode)
	{
#if VSP_PLATFORM_WINDOWS
		// ExitProcess (not ::TerminateProcess on our own handle) so the CRT and
		// every loaded DLL still get their detach notification, which is what
		// closes the log file cleanly.
		::ExitProcess(static_cast<UINT>(nExitCode));
#else
		// Portable fallback: a normal, non-zero exit.
		std::exit(static_cast<int>(nExitCode));
#endif
	}

	// -------------------------------------------------------------------------
	// Time
	// -------------------------------------------------------------------------

	uint64 PlatformMisc::GetElapsedMilliseconds()
	{
#if VSP_PLATFORM_WINDOWS
		return static_cast<uint64>(::GetTickCount64());
#else
		return 0;
#endif
	}

	// -------------------------------------------------------------------------
	// Threads
	// -------------------------------------------------------------------------

	uint64 PlatformMisc::GetCurrentThreadId()
	{
#if VSP_PLATFORM_WINDOWS
		return static_cast<uint64>(::GetCurrentThreadId());
#else
		// No thread identity on this platform yet: report 0, which the callers
		// read as "unknown" and never report on - refusing to guess keeps the
		// engine's own thread rule honest instead of producing false reports.
		return 0;
#endif
	}

	// -------------------------------------------------------------------------
	// HighResolutionTimer
	// -------------------------------------------------------------------------

	float HighResolutionTimer::Tick()
	{
#if VSP_PLATFORM_WINDOWS
		if (!m_bHasLastTick)
		{
			LARGE_INTEGER nTimerFrequency;
			LARGE_INTEGER nLastTickCounter;
			QueryPerformanceFrequency(&nTimerFrequency);
			QueryPerformanceCounter(&nLastTickCounter);
			m_nTimerFrequency = nTimerFrequency.QuadPart;
			m_nLastTickCounter = nLastTickCounter.QuadPart;
			m_bHasLastTick = true;
			return 0.0f;
		}

		LARGE_INTEGER nCurrentTickCounter;
		QueryPerformanceCounter(&nCurrentTickCounter);

		const double dDeltaSeconds =
			static_cast<double>(nCurrentTickCounter.QuadPart - m_nLastTickCounter) /
			static_cast<double>(m_nTimerFrequency);
		m_nLastTickCounter = nCurrentTickCounter.QuadPart;

		float fDeltaSeconds = static_cast<float>(dDeltaSeconds);
		if (fDeltaSeconds > k_fMaximumDeltaSeconds)
		{
			fDeltaSeconds = k_fMaximumDeltaSeconds;
		}
		return fDeltaSeconds;
#else
		// Portable fallback: steady_clock with the same clamp semantics.
		using namespace std::chrono;
		static steady_clock::time_point s_nLastTick = steady_clock::now();
		const steady_clock::time_point nCurrentTick = steady_clock::now();
		const float fDeltaSeconds = duration_cast<duration<float>>(nCurrentTick - s_nLastTick).count();
		s_nLastTick = nCurrentTick;
		return fDeltaSeconds > k_fMaximumDeltaSeconds ? k_fMaximumDeltaSeconds : fDeltaSeconds;
#endif
	}
}
