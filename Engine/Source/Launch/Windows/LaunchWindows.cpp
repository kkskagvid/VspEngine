#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <shellapi.h>

#include "Core/Core.h"

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
	// Parse the wide command line the same way the CRT would.
	int nArgumentCount = 0;
	LPWSTR* pArguments = CommandLineToArgvW(GetCommandLineW(), &nArgumentCount);
	if (pArguments == nullptr)
	{
		return 1;
	}

	const int32 nExitCode = Vsp::RunLaunchLoop(nArgumentCount, pArguments);

	LocalFree(pArguments);
	return nExitCode;
}
