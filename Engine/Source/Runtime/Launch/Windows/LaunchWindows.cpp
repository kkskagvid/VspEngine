// -------------------------------------------------------------------------
// Launch entry point (Windows).
//
// The whole translation unit is Windows-only, which is why it is guarded: the
// file is named by the project, but a build that globs sources (or a CMake
// port) has to be ABLE to see why it does not belong on another platform. The
// platform-independent host lives in LaunchLoop.cpp; what CANNOT move out of
// the executable module is here, because Windows requires it:
//   - the wWinMain entry point, and
//   - the GPU-selection exports (they only work when exported from the .exe).
// Everything else Windows-only stays behind Common/PlatformMisc.
// -------------------------------------------------------------------------

#include "Core/Platform.h"

#if VSP_PLATFORM_WINDOWS

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <shellapi.h>

#include "Core/Core.h"
#include "Core/Logging/Log.h"

// http://developer.download.nvidia.com/devzone/devcenter/gamegraphics/files/OptimuxRenderingPolicies.pdf
// The following line is to favor the high performance NVIDIA GPU if there are multiple GPUs
// Has to be .exe module to be correctly detected.
extern "C" { __declspec(dllexport) uint32 NvOptimusEnablement = 0x00000001; }

// And the AMD equivalent
// Also has to be .exe module to be correctly detected.
extern "C" { __declspec(dllexport) uint32 AmdPowerXpressRequestHighPerformance = 0x00000001; }

namespace Vsp
{
	// Declared in LaunchLoop.cpp.
	int32 RunLaunchLoop(int32 nArgumentCount, wchar_t** pArguments);
}

int WINAPI wWinMain(
	_In_ HINSTANCE hInstance,
	_In_opt_ HINSTANCE hPrevInstance,
	_In_ LPWSTR lpCmdLine,
	_In_ int nShowCmd)
{
	// Parse the wide command line the way the CRT does. The parsing itself is a
	// Windows call, so it happens here - in the platform entry - and the host
	// below only ever sees the argument array.
	int32 nArgumentCount = 0;
	LPWSTR* pArguments = ::CommandLineToArgvW(::GetCommandLineW(), &nArgumentCount);
	if (pArguments == nullptr)
	{
		LOG_ERROR("Launch", "The command line could not be parsed (CommandLineToArgvW failed).");
		return 1;
	}

	const int32 nExitCode = Vsp::RunLaunchLoop(nArgumentCount, pArguments);

	::LocalFree(pArguments);
	return nExitCode;
}

#endif   // VSP_PLATFORM_WINDOWS
