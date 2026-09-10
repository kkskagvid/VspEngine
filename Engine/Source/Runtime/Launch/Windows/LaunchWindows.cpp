// -------------------------------------------------------------------------
// Launch entry point (Windows).
//
// Everything Windows-only lives in Common behind #if VSP_PLATFORM_WINDOWS;
// the two pieces below CANNOT move there, because Windows requires them to
// be in the executable module itself:
//   - the wWinMain entry point, and
//   - the GPU-selection exports (they only work when exported from the .exe).
// The command-line parsing itself is encapsulated in Common/PlatformMisc.
// -------------------------------------------------------------------------

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <shellapi.h>

#include "Core/Core.h"
#include "Core/Logging/Log.h"

// http://developer.download.nvidia.com/devzone/devcenter/gamegraphics/files/OptimusRenderingPolicies.pdf
// The following line is to favor the high performance NVIDIA GPU if there are multiple GPUs
// Has to be .exe module to be correctly detected.
extern "C" { _declspec(dllexport) uint32 NvOptimusEnablement = 0x00000001; }

// And the AMD equivalent
// Also has to be .exe module to be correctly detected.
extern "C" { _declspec(dllexport) uint32 AmdPowerXpressRequestHighPerformance = 0x00000001; }

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
	// Parse the wide command line the same way the CRT would (the parsing is
	// encapsulated in Common/PlatformMisc behind #if VSP_PLATFORM_WINDOWS).
	int32 nArgumentCount = 0;
	LPWSTR* pArguments = ::CommandLineToArgvW(::GetCommandLineW(), &nArgumentCount);
	if (pArguments == nullptr)
	{
		return 1;
	}

	const int32 nExitCode = Vsp::RunLaunchLoop(nArgumentCount, pArguments);

	::LocalFree(pArguments);
	return nExitCode;
}
