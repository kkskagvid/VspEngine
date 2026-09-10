#include "RuntimePCH.h"

#include <chrono>

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
