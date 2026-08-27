#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <shellapi.h>

#include "Core/Core.h"

namespace Vsp
{
	// Declared in LaunchLoop.cpp.
	int32_t RunLaunchLoop(int32_t nArgumentCount, wchar_t** pArguments);
}

int WINAPI WinMain(
	HINSTANCE hInstance,
	HINSTANCE hPrevInstance,
	LPSTR pCommandLine,
	int nShowCommand)
{
	// Parse the wide command line the same way the CRT would.
	int nArgumentCount = 0;
	LPWSTR* pArguments = CommandLineToArgvW(GetCommandLineW(), &nArgumentCount);
	if (pArguments == nullptr)
	{
		return 1;
	}

	const int32_t nExitCode = Vsp::RunLaunchLoop(nArgumentCount, pArguments);

	LocalFree(pArguments);
	return nExitCode;
}
