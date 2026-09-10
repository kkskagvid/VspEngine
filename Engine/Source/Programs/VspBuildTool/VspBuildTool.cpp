// -------------------------------------------------------------------------
// VspBuildTool
// -------------------------------------------------------------------------
// Lightweight build system / stager for the engine.
//
//   - (optional) invokes MSBuild on VspEngine.slnx,
//   - copies the runtime executables (VspCore = the native runtime,
//     VspEngine + Assembly = the managed assemblies, Launch = the host)
//     together with the necessary companion files into the run directory,
//   - ensures the C# runtime is reachable from the run directory through
//     the same relative layout the engine resolves ("..\..\..\Binaries\dotnet").
//
// The staged run directory follows the structure of the current
// Intermediate\Binaries\Debug_x64 directory: everything the host needs at
// runtime sits flat next to Launch.exe.
//
// Windows-only file operations are kept behind #if VSP_PLATFORM_WINDOWS,
// mirroring the engine's Common encapsulation pattern.
//
// Requirements honored here: C++20, MSVC, no exceptions, PascalCase
// function names, Hungarian field notation.
// -------------------------------------------------------------------------

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>

#if VSP_PLATFORM_WINDOWS
	#define WIN32_LEAN_AND_MEAN
	#include <Windows.h>
#endif

using int8   = int8_t;
using int32  = int32_t;
using int64  = int64_t;
using uint8  = uint8_t;
using uint32 = uint32_t;
using uint64 = uint64_t;

// -------------------------------------------------------------------------
// Constants
// -------------------------------------------------------------------------

static constexpr uint32 k_nMaxPathLength = 1024;
static constexpr uint32 k_nMaxCommandLineLength = 8192;
static constexpr wchar_t k_cPathSeparator = L'\\';
static constexpr const wchar_t* k_sSolutionFileName = L"VspEngine.slnx";
static constexpr const wchar_t* k_sDotNetRuntimeName = L"runtime10.0.10";

// The engine resolves the C# runtime relative to the executable directory:
//   <exeDir>\..\..\..\Binaries\dotnet\runtime10.0.10
static constexpr const wchar_t* k_sDotNetRuntimeRelativePath =
	L"..\\..\\..\\Binaries\\dotnet\\runtime10.0.10";

// -------------------------------------------------------------------------
// Options
// -------------------------------------------------------------------------

struct BuildToolOptions
{
	wchar_t sRootPath[k_nMaxPathLength] = {};        // Repository root (VspEngine.slnx lives here).
	wchar_t sOutputDirectory[k_nMaxPathLength] = {}; // Run directory to stage into.
	wchar_t sConfiguration[32] = L"Debug";
	wchar_t sPlatform[32] = L"x64";
	wchar_t sMSBuildPath[k_nMaxPathLength] = {};     // Explicit MSBuild.exe override.
	wchar_t sRuntimeSource[k_nMaxPathLength] = {};   // Explicit dotnet runtime source dir.
	bool bBuildFirst = false;                        // Invoke MSBuild before staging.
	bool bCleanRunDirectory = false;                 // Delete the run directory first.
	bool bListOnly = false;                          // Print the plan without writing.
	bool bVerbose = false;
	bool bShowHelp = false;
};

enum class FileCopyResult : uint32
{
	Copied = 0,
	UpToDate = 1,
	Failed = 2,
};

// -------------------------------------------------------------------------
// Logging / utilities
// -------------------------------------------------------------------------

static void PrintLine(const wchar_t* pFormat, ...)
{
	va_list pArguments;
	va_start(pArguments, pFormat);
	vfwprintf(stdout, pFormat, pArguments);
	va_end(pArguments);
	fwprintf(stdout, L"\n");
}

static void PrintError(const wchar_t* pFormat, ...)
{
	va_list pArguments;
	va_start(pArguments, pFormat);
	fwprintf(stderr, L"[VspBuildTool] error: ");
	vfwprintf(stderr, pFormat, pArguments);
	va_end(pArguments);
	fwprintf(stderr, L"\n");
}

// Case-insensitive wide-string equality (option names).
static bool WideTextEqualsIgnoreCase(const wchar_t* pLeft, const wchar_t* pRight)
{
	return _wcsicmp(pLeft, pRight) == 0;
}

// Joins a directory and a name with one separator. Returns false when the
// result does not fit into the buffer.
static bool JoinPath(wchar_t* pOutPath, uint32 uCapacity, const wchar_t* pDirectory, const wchar_t* pName)
{
	if (pOutPath == nullptr || pDirectory == nullptr || pName == nullptr || uCapacity == 0)
	{
		return false;
	}

	const size_t nDirectoryLength = wcslen(pDirectory);
	const size_t nNameLength = wcslen(pName);

	// +2: one separator (if needed) + one null terminator.
	if (nDirectoryLength + nNameLength + 2 > uCapacity)
	{
		return false;
	}

	wcscpy_s(pOutPath, uCapacity, pDirectory);

	if (nDirectoryLength > 0)
	{
		const wchar_t cLastCharacter = pDirectory[nDirectoryLength - 1];
		if (cLastCharacter != k_cPathSeparator && cLastCharacter != L'/')
		{
			wcscat_s(pOutPath, uCapacity, L"\\");
		}
	}

	wcscat_s(pOutPath, uCapacity, pName);
	return true;
}

// Creates every missing directory on the path (one level at a time).
static bool EnsureDirectoryExists(const wchar_t* pDirectoryPath)
{
	if (pDirectoryPath == nullptr || pDirectoryPath[0] == L'\0')
	{
		return false;
	}

#if VSP_PLATFORM_WINDOWS
	wchar_t sCurrentPath[k_nMaxPathLength] = {};
	wcscpy_s(sCurrentPath, k_nMaxPathLength, pDirectoryPath);

	// Normalize forward slashes so the prefix logic below only handles one
	// separator kind.
	for (size_t nIndex = 0; sCurrentPath[nIndex] != L'\0'; ++nIndex)
	{
		if (sCurrentPath[nIndex] == L'/')
		{
			sCurrentPath[nIndex] = k_cPathSeparator;
		}
	}

	// Skip the drive root / UNC prefix ("C:\" or "\\server\share\").
	size_t nStartIndex = 0;
	if (sCurrentPath[0] != L'\0' && sCurrentPath[1] == L':')
	{
		nStartIndex = 3;   // "C:\"
	}
	else if (sCurrentPath[0] == k_cPathSeparator && sCurrentPath[1] == k_cPathSeparator)
	{
		nStartIndex = 2;
	}

	for (size_t nIndex = nStartIndex; sCurrentPath[nIndex] != L'\0'; ++nIndex)
	{
		if (sCurrentPath[nIndex] != k_cPathSeparator)
		{
			continue;
		}

		sCurrentPath[nIndex] = L'\0';
		if (sCurrentPath[0] != L'\0')
		{
			::CreateDirectoryW(sCurrentPath, nullptr);
		}
		sCurrentPath[nIndex] = k_cPathSeparator;
	}

	if (sCurrentPath[0] != L'\0')
	{
		::CreateDirectoryW(sCurrentPath, nullptr);
	}

	const DWORD nAttributes = ::GetFileAttributesW(pDirectoryPath);
	return nAttributes != INVALID_FILE_ATTRIBUTES &&
		(nAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
	return false;
#endif
}

// True when the file at the given path exists.
static bool FileExists(const wchar_t* pFilePath)
{
#if VSP_PLATFORM_WINDOWS
	const DWORD nAttributes = ::GetFileAttributesW(pFilePath);
	return nAttributes != INVALID_FILE_ATTRIBUTES &&
		(nAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
#else
	return false;
#endif
}

// True when the directory at the given path exists.
static bool DirectoryExists(const wchar_t* pDirectoryPath)
{
#if VSP_PLATFORM_WINDOWS
	const DWORD nAttributes = ::GetFileAttributesW(pDirectoryPath);
	return nAttributes != INVALID_FILE_ATTRIBUTES &&
		(nAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
	return false;
#endif
}

// Copies one file when it is missing or newer than the destination.
static FileCopyResult CopyFileIfNewer(
	const wchar_t* pSourcePath,
	const wchar_t* pDestinationPath,
	bool bVerbose)
{
	if (!FileExists(pSourcePath))
	{
		PrintError(L"source file does not exist: %ls", pSourcePath);
		return FileCopyResult::Failed;
	}

#if VSP_PLATFORM_WINDOWS
	// Skip the copy when the destination is already identical.
	if (FileExists(pDestinationPath))
	{
		WIN32_FILE_ATTRIBUTE_DATA sourceData = {};
		WIN32_FILE_ATTRIBUTE_DATA destinationData = {};
		if (::GetFileAttributesExW(pSourcePath, GetFileExInfoStandard, &sourceData) != 0 &&
			::GetFileAttributesExW(pDestinationPath, GetFileExInfoStandard, &destinationData) != 0 &&
			sourceData.nFileSizeHigh == destinationData.nFileSizeHigh &&
			sourceData.nFileSizeLow == destinationData.nFileSizeLow &&
			sourceData.ftLastWriteTime.dwHighDateTime == destinationData.ftLastWriteTime.dwHighDateTime &&
			sourceData.ftLastWriteTime.dwLowDateTime == destinationData.ftLastWriteTime.dwLowDateTime)
		{
			if (bVerbose)
			{
				PrintLine(L"  up to date: %ls", pDestinationPath);
			}
			return FileCopyResult::UpToDate;
		}
	}

	if (!::CopyFileW(pSourcePath, pDestinationPath, FALSE))
	{
		PrintError(L"failed to copy '%ls' -> '%ls' (Win32 error %d).",
			pSourcePath, pDestinationPath, static_cast<int32>(::GetLastError()));
		return FileCopyResult::Failed;
	}
#endif

	if (bVerbose)
	{
		PrintLine(L"  copied: %ls", pDestinationPath);
	}
	return FileCopyResult::Copied;
}

// Recursively copies a whole directory tree; returns the copied file count.
static uint32 CopyDirectoryTree(
	const wchar_t* pSourceDirectory,
	const wchar_t* pDestinationDirectory,
	bool bVerbose,
	bool& outAllSucceeded)
{
	uint32 uCopiedCount = 0;

#if VSP_PLATFORM_WINDOWS
	wchar_t sSearchPattern[k_nMaxPathLength] = {};
	if (!JoinPath(sSearchPattern, k_nMaxPathLength, pSourceDirectory, L"*"))
	{
		outAllSucceeded = false;
		return 0;
	}

	WIN32_FIND_DATAW findData = {};
	const HANDLE hFindHandle = ::FindFirstFileW(sSearchPattern, &findData);
	if (hFindHandle == INVALID_HANDLE_VALUE)
	{
		PrintError(L"FindFirstFileW failed for '%ls' (Win32 error %d).",
			sSearchPattern, static_cast<int32>(::GetLastError()));
		outAllSucceeded = false;
		return 0;
	}

	do
	{
		if (wcscmp(findData.cFileName, L".") == 0 || wcscmp(findData.cFileName, L"..") == 0)
		{
			continue;
		}

		wchar_t sSourceChild[k_nMaxPathLength] = {};
		wchar_t sDestinationChild[k_nMaxPathLength] = {};
		if (!JoinPath(sSourceChild, k_nMaxPathLength, pSourceDirectory, findData.cFileName) ||
			!JoinPath(sDestinationChild, k_nMaxPathLength, pDestinationDirectory, findData.cFileName))
		{
			outAllSucceeded = false;
			continue;
		}

		if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
		{
			uCopiedCount += CopyDirectoryTree(
				sSourceChild, sDestinationChild, bVerbose, outAllSucceeded);
		}
		else
		{
			if (!EnsureDirectoryExists(pDestinationDirectory))
			{
				PrintError(L"failed to create directory '%ls'.", pDestinationDirectory);
				outAllSucceeded = false;
				continue;
			}

			const FileCopyResult eResult = CopyFileIfNewer(sSourceChild, sDestinationChild, bVerbose);
			if (eResult == FileCopyResult::Failed)
			{
				outAllSucceeded = false;
			}
			else if (eResult == FileCopyResult::Copied)
			{
				++uCopiedCount;
			}
		}
	} while (::FindNextFileW(hFindHandle, &findData) != 0);

	::FindClose(hFindHandle);
#else
	outAllSucceeded = false;
#endif

	return uCopiedCount;
}

// Recursively deletes a directory tree.
static void RemoveDirectoryTree(const wchar_t* pDirectoryPath)
{
#if VSP_PLATFORM_WINDOWS
	if (!DirectoryExists(pDirectoryPath))
	{
		return;
	}

	wchar_t sSearchPattern[k_nMaxPathLength] = {};
	if (!JoinPath(sSearchPattern, k_nMaxPathLength, pDirectoryPath, L"*"))
	{
		return;
	}

	WIN32_FIND_DATAW findData = {};
	const HANDLE hFindHandle = ::FindFirstFileW(sSearchPattern, &findData);
	if (hFindHandle != INVALID_HANDLE_VALUE)
	{
		do
		{
			if (wcscmp(findData.cFileName, L".") == 0 || wcscmp(findData.cFileName, L"..") == 0)
			{
				continue;
			}

			wchar_t sChildPath[k_nMaxPathLength] = {};
			if (!JoinPath(sChildPath, k_nMaxPathLength, pDirectoryPath, findData.cFileName))
			{
				continue;
			}

			if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
			{
				RemoveDirectoryTree(sChildPath);
			}
			else
			{
				::SetFileAttributesW(sChildPath, FILE_ATTRIBUTE_NORMAL);
				::DeleteFileW(sChildPath);
			}
		} while (::FindNextFileW(hFindHandle, &findData) != 0);

		::FindClose(hFindHandle);
	}

	::RemoveDirectoryW(pDirectoryPath);
#endif
}

// Resolves a full path from a possibly relative one (relative to the CWD).
static void ResolveFullPath(wchar_t* pOutPath, uint32 uCapacity, const wchar_t* pInputPath)
{
#if VSP_PLATFORM_WINDOWS
	::GetFullPathNameW(pInputPath, uCapacity, pOutPath, nullptr);
#else
	if (pOutPath != nullptr && pInputPath != nullptr)
	{
		wcscpy_s(pOutPath, uCapacity, pInputPath);
	}
#endif
}

// -------------------------------------------------------------------------
// Staging plan
// -------------------------------------------------------------------------

// One file the stager copies into the run directory.
struct StageFileEntry
{
	const wchar_t* pFileName;
	const wchar_t* pAlternateSourcePath;   // Used when the bin dir does not hold the file.
};

// Every file the host needs next to Launch.exe. The order mirrors the
// Intermediate\Binaries\Debug_x64 layout (flat, next to the executable).
static const StageFileEntry k_sStageFiles[] =
{
	{ L"Launch.exe",              nullptr },
	{ L"Launch.pdb",              nullptr },
	{ L"Launch.runtimeconfig.json", L"Engine\\Source\\Runtime\\Launch\\Config\\Launch.runtimeconfig.json" },
	{ L"VspCore.dll",             nullptr },
	{ L"VspCore.pdb",             nullptr },
	{ L"VspEngine.dll",           nullptr },
	{ L"VspEngine.pdb",           nullptr },
	{ L"VspEngine.deps.json",     nullptr },
	{ L"Assembly.dll",            nullptr },
	{ L"Assembly.pdb",            nullptr },
	{ L"Assembly.deps.json",      nullptr },
	{ L"vulkan-1.dll",            nullptr },
};
static constexpr uint32 k_nStageFileCount =
	static_cast<uint32>(sizeof(k_sStageFiles) / sizeof(k_sStageFiles[0]));

// -------------------------------------------------------------------------
// MSBuild
// -------------------------------------------------------------------------

// Runs a process and captures its standard output into the given buffer.
// Returns the process exit code, or -1 when the process could not start.
static int32 RunProcessCapturingOutput(
	const wchar_t* pExecutablePath,
	const wchar_t* pCommandLine,
	const wchar_t* pWorkingDirectory,
	wchar_t* pOutText,
	uint32 uOutCapacity)
{
#if VSP_PLATFORM_WINDOWS
	if (pOutText != nullptr && uOutCapacity > 0)
	{
		pOutText[0] = L'\0';
	}

	// Capture the child's output through a plain FILE (named pipes are not
	// used, so this works in confined environments too).
	wchar_t sOutputFile[k_nMaxPathLength] = {};
	if (::GetTempPathW(k_nMaxPathLength, sOutputFile) == 0)
	{
		wcscpy_s(sOutputFile, k_nMaxPathLength, L".");
	}
	JoinPath(sOutputFile, k_nMaxPathLength, sOutputFile, L"vspbuildtool_output.txt");

	SECURITY_ATTRIBUTES securityAttributes = {};
	securityAttributes.nLength = sizeof(SECURITY_ATTRIBUTES);
	securityAttributes.bInheritHandle = TRUE;

	const HANDLE hOutputFile = ::CreateFileW(
		sOutputFile, GENERIC_WRITE, FILE_SHARE_READ, &securityAttributes,
		CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

	wchar_t sMutableCommandLine[k_nMaxCommandLineLength] = {};
	wcscpy_s(sMutableCommandLine, k_nMaxCommandLineLength, pCommandLine);

	STARTUPINFOW startupInfo = {};
	startupInfo.cb = sizeof(STARTUPINFOW);
	startupInfo.hStdOutput = hOutputFile;
	startupInfo.hStdError = hOutputFile;
	startupInfo.dwFlags = STARTF_USESTDHANDLES;

	PROCESS_INFORMATION processInformation = {};
	const BOOL bCreated = ::CreateProcessW(
		pExecutablePath,
		sMutableCommandLine,
		nullptr,
		nullptr,
		TRUE,                 // Inherit the output handles.
		0,
		nullptr,
		pWorkingDirectory,
		&startupInfo,
		&processInformation);
	if (!bCreated)
	{
		PrintError(L"failed to start '%ls' (Win32 error %d).",
			pExecutablePath, static_cast<int32>(::GetLastError()));
		if (hOutputFile != INVALID_HANDLE_VALUE)
		{
			::CloseHandle(hOutputFile);
		}
		return -1;
	}

	::WaitForSingleObject(processInformation.hProcess, INFINITE);

	DWORD nExitCode = 0;
	::GetExitCodeProcess(processInformation.hProcess, &nExitCode);
	::CloseHandle(processInformation.hThread);
	::CloseHandle(processInformation.hProcess);
	if (hOutputFile != INVALID_HANDLE_VALUE)
	{
		::CloseHandle(hOutputFile);
	}

	// Read the captured output back. The child writes bytes in the ANSI
	// code page, so convert them to wide text instead of reinterpreting.
	if (pOutText != nullptr && uOutCapacity > 0)
	{
		pOutText[0] = L'\0';
		const HANDLE hReadFile = ::CreateFileW(
			sOutputFile, GENERIC_READ, FILE_SHARE_READ, nullptr,
			OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (hReadFile != INVALID_HANDLE_VALUE)
		{
			const DWORD nByteCapacity = (uOutCapacity - 1) * sizeof(wchar_t);
			char* pAnsiBytes = static_cast<char*>(::LocalAlloc(LPTR, nByteCapacity));
			if (pAnsiBytes != nullptr)
			{
				DWORD nBytesRead = 0;
				if (::ReadFile(hReadFile, pAnsiBytes, nByteCapacity, &nBytesRead, nullptr) && nBytesRead > 0)
				{
					const int nConvertedLength = ::MultiByteToWideChar(
						CP_ACP, 0, pAnsiBytes, static_cast<int>(nBytesRead),
						pOutText, static_cast<int>(uOutCapacity - 1));
					pOutText[nConvertedLength >= 0 ? nConvertedLength : 0] = L'\0';
				}
				::LocalFree(pAnsiBytes);
			}
			::CloseHandle(hReadFile);
		}
	}

	::DeleteFileW(sOutputFile);
	return static_cast<int32>(nExitCode);
#else
	return -1;
#endif
}

// Locates MSBuild.exe: explicit option -> MSBUILD env -> vswhere -> known
// installation paths. Returns true and fills pOutPath on success.
static bool FindMSBuildExecutable(wchar_t* pOutPath, uint32 uCapacity, const BuildToolOptions& options)
{
	if (pOutPath == nullptr || uCapacity == 0)
	{
		return false;
	}

	// 1. Explicit override.
	if (options.sMSBuildPath[0] != L'\0')
	{
		wcscpy_s(pOutPath, uCapacity, options.sMSBuildPath);
		return FileExists(pOutPath);
	}

	// 2. MSBUILD environment variable (fall through when it does not exist).
#if VSP_PLATFORM_WINDOWS
	{
		wchar_t sEnvironmentPath[k_nMaxPathLength] = {};
		if (::GetEnvironmentVariableW(L"MSBUILD", sEnvironmentPath, k_nMaxPathLength) > 0 &&
			FileExists(sEnvironmentPath))
		{
			wcscpy_s(pOutPath, uCapacity, sEnvironmentPath);
			return true;
		}
	}
#endif

	// 3. vswhere (Visual Studio installer location service).
#if VSP_PLATFORM_WINDOWS
	{
		const wchar_t* kVswherePath =
			L"C:\\Program Files (x86)\\Microsoft Visual Studio\\Installer\\vswhere.exe";
		if (FileExists(kVswherePath))
		{
			const wchar_t* kVswhereArguments =
				L"-latest -products * -requires Microsoft.Component.MSBuild -property installationPath";
			wchar_t sInstallationRoot[k_nMaxPathLength] = {};
			const int32 nExitCode = RunProcessCapturingOutput(
				kVswherePath, kVswhereArguments, nullptr, sInstallationRoot, k_nMaxPathLength);
			if (nExitCode == 0 && sInstallationRoot[0] != L'\0')
			{
				// Strip the trailing newline vswhere appends.
				size_t nLength = wcslen(sInstallationRoot);
				while (nLength > 0 &&
					(sInstallationRoot[nLength - 1] == L'\n' || sInstallationRoot[nLength - 1] == L'\r'))
				{
					sInstallationRoot[--nLength] = L'\0';
				}

				// Fall through to the known paths when the joined path does
				// not exist: a stale vswhere result must not hide a working
				// installation.
				if (JoinPath(pOutPath, uCapacity, sInstallationRoot,
					L"MSBuild\\Current\\Bin\\MSBuild.exe") &&
					FileExists(pOutPath))
				{
					return true;
				}
			}
		}
	}
#endif

	// 4. Known installation paths.
	static const wchar_t* const k_sKnownMSBuildPaths[] =
	{
		L"C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\MSBuild\\Current\\Bin\\MSBuild.exe",
		L"C:\\Program Files\\Microsoft Visual Studio\\18\\Professional\\MSBuild\\Current\\Bin\\MSBuild.exe",
		L"C:\\Program Files\\Microsoft Visual Studio\\18\\Enterprise\\MSBuild\\Current\\Bin\\MSBuild.exe",
		L"C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\MSBuild\\Current\\Bin\\MSBuild.exe",
		L"C:\\Program Files\\Microsoft Visual Studio\\2022\\Professional\\MSBuild\\Current\\Bin\\MSBuild.exe",
		L"C:\\Program Files\\Microsoft Visual Studio\\2022\\Enterprise\\MSBuild\\Current\\Bin\\MSBuild.exe",
	};
	for (const wchar_t* pCandidate : k_sKnownMSBuildPaths)
	{
		if (FileExists(pCandidate))
		{
			wcscpy_s(pOutPath, uCapacity, pCandidate);
			return true;
		}
	}

	return false;
}

// Invokes MSBuild on the solution. Returns true when the build succeeded.
static bool InvokeMSBuild(const wchar_t* pMSBuildPath, const BuildToolOptions& options)
{
	wchar_t sCommandLine[k_nMaxCommandLineLength] = {};
	swprintf_s(sCommandLine, k_nMaxCommandLineLength,
		L"\"%ls\" %ls /restore /p:Configuration=%ls /p:Platform=%ls /v:m /nologo",
		pMSBuildPath,
		k_sSolutionFileName,
		options.sConfiguration,
		options.sPlatform);

	PrintLine(L"Running: %ls", sCommandLine);

	wchar_t sBuildOutput[65536] = {};
	const int32 nExitCode = RunProcessCapturingOutput(
		pMSBuildPath, sCommandLine, options.sRootPath, sBuildOutput, 65536);
	if (sBuildOutput[0] != L'\0')
	{
		fwprintf(stdout, L"%ls", sBuildOutput);
	}

	if (nExitCode != 0)
	{
		PrintError(L"MSBuild failed with exit code %d.", nExitCode);
		return false;
	}
	return true;
}

// -------------------------------------------------------------------------
// Staging
// -------------------------------------------------------------------------

// Resolves the source path for one stage file: the binaries directory
// first, then the per-file alternate path, then the Vulkan SDK locations
// for the loader. Returns true when a source file was found.
static bool ResolveStageFileSource(
	const BuildToolOptions& options,
	const wchar_t* pBinariesDirectory,
	const StageFileEntry& entry,
	wchar_t* pOutSourcePath,
	uint32 uCapacity)
{
	if (pOutSourcePath == nullptr || uCapacity == 0)
	{
		return false;
	}

	JoinPath(pOutSourcePath, uCapacity, pBinariesDirectory, entry.pFileName);

	if (!FileExists(pOutSourcePath) && entry.pAlternateSourcePath != nullptr)
	{
		wchar_t sAlternatePath[k_nMaxPathLength] = {};
		swprintf_s(sAlternatePath, k_nMaxPathLength, L"%ls\\%ls",
			options.sRootPath, entry.pAlternateSourcePath);
		wcscpy_s(pOutSourcePath, uCapacity, sAlternatePath);
	}

	// The Vulkan loader is not built by the solution: pick it up from the
	// Vulkan SDK installation (same order as the Launch post-build step).
	if (!FileExists(pOutSourcePath) && wcscmp(entry.pFileName, L"vulkan-1.dll") == 0)
	{
		wchar_t sVulkanSdkRoot[k_nMaxPathLength] = {};
#if VSP_PLATFORM_WINDOWS
		::GetEnvironmentVariableW(L"VULKAN_SDK", sVulkanSdkRoot, k_nMaxPathLength);
#endif
		if (sVulkanSdkRoot[0] != L'\\0')
		{
			JoinPath(pOutSourcePath, uCapacity, sVulkanSdkRoot, L"Bin\\vulkan-1.dll");
		}

		if (!FileExists(pOutSourcePath))
		{
			swprintf_s(pOutSourcePath, uCapacity,
				L"%ls\\Engine\\Source\\Thirdparty\\Vulkan\\Bin\\vulkan-1.dll",
				options.sRootPath);
		}
	}

	return FileExists(pOutSourcePath);
}

// Stages every runtime file into the run directory. Returns true when every
// required file was copied (missing optional files are warnings).
static bool StageRuntimeFiles(const BuildToolOptions& options)
{
	wchar_t sBinariesDirectory[k_nMaxPathLength] = {};
	swprintf_s(sBinariesDirectory, k_nMaxPathLength,
		L"%ls\\Engine\\Intermediate\\Binaries\\%ls_%ls",
		options.sRootPath, options.sConfiguration, options.sPlatform);

	uint32 uCopiedCount = 0;
	uint32 uUpToDateCount = 0;
	uint32 uFailedCount = 0;
	uint32 uSkippedOptionalCount = 0;

	PrintLine(L"Staging runtime files:");
	PrintLine(L"  run directory : %ls", options.sOutputDirectory);
	PrintLine(L"  source        : %ls", sBinariesDirectory);

	for (uint32 uIndex = 0; uIndex < k_nStageFileCount; ++uIndex)
	{
		const StageFileEntry& entry = k_sStageFiles[uIndex];

		wchar_t sSourcePath[k_nMaxPathLength] = {};
		const bool bHasSource = ResolveStageFileSource(
			options, sBinariesDirectory, entry, sSourcePath, k_nMaxPathLength);

		wchar_t sDestinationPath[k_nMaxPathLength] = {};
		JoinPath(sDestinationPath, k_nMaxPathLength, options.sOutputDirectory, entry.pFileName);

		if (!bHasSource)
		{
			// vulkan-1.dll is optional: the loader may find it through the
			// system search path when neither source location provides it.
			if (wcscmp(entry.pFileName, L"vulkan-1.dll") == 0)
			{
				PrintLine(L"  warning: %ls not found (looked in '%ls'); skipped.",
					entry.pFileName, sBinariesDirectory);
				++uSkippedOptionalCount;
				continue;
			}

			PrintError(L"source file missing for '%ls' (looked in '%ls').",
				entry.pFileName, sBinariesDirectory);
			++uFailedCount;
			continue;
		}

		if (!EnsureDirectoryExists(options.sOutputDirectory))
		{
			PrintError(L"failed to create the run directory '%ls'.", options.sOutputDirectory);
			return false;
		}

		const FileCopyResult eResult = CopyFileIfNewer(sSourcePath, sDestinationPath, options.bVerbose);
		if (eResult == FileCopyResult::Copied)
		{
			++uCopiedCount;
		}
		else if (eResult == FileCopyResult::UpToDate)
		{
			++uUpToDateCount;
		}
		else
		{
			++uFailedCount;
		}
	}

	PrintLine(L"staged: %d copied, %d up to date, %d skipped (optional), %d failed.",
		uCopiedCount, uUpToDateCount, uSkippedOptionalCount, uFailedCount);
	return uFailedCount == 0;
}

// Resolves the directory the engine will look the C# runtime up in and makes
// sure the runtime tree is present there (copying it when missing). Returns
// false on failure.
static bool EnsureDotNetRuntime(const BuildToolOptions& options, bool bListOnly)
{
	// <output>\..\..\..\Binaries\dotnet\runtime10.0.10
	// (Join and resolve use separate buffers: GetFullPathNameW must not
	// receive overlapping input/output buffers.)
	wchar_t sJoinedPath[k_nMaxPathLength] = {};
	if (!JoinPath(sJoinedPath, k_nMaxPathLength, options.sOutputDirectory, k_sDotNetRuntimeRelativePath))
	{
		PrintError(L"runtime path does not fit into the path buffer.");
		return false;
	}

	wchar_t sResolvedDirectory[k_nMaxPathLength] = {};
	ResolveFullPath(sResolvedDirectory, k_nMaxPathLength, sJoinedPath);

	// The runtime is considered present when its host/nethost.dll exists.
	wchar_t sHostDirectory[k_nMaxPathLength] = {};
	JoinPath(sHostDirectory, k_nMaxPathLength, sResolvedDirectory, L"host");
	wchar_t sMarkerPath[k_nMaxPathLength] = {};
	JoinPath(sMarkerPath, k_nMaxPathLength, sHostDirectory, L"nethost.dll");

	if (FileExists(sMarkerPath))
	{
		PrintLine(L"  C# runtime verified at %ls", sResolvedDirectory);
		return true;
	}

	// Resolve the runtime source.
	wchar_t sRuntimeSource[k_nMaxPathLength] = {};
	if (options.sRuntimeSource[0] != L'\0')
	{
		wcscpy_s(sRuntimeSource, k_nMaxPathLength, options.sRuntimeSource);
	}
	else
	{
		swprintf_s(sRuntimeSource, k_nMaxPathLength, L"%ls\\Engine\\Binaries\\dotnet\\%ls",
			options.sRootPath, k_sDotNetRuntimeName);
	}

	if (!DirectoryExists(sRuntimeSource))
	{
		PrintError(L"C# runtime missing at '%ls' and the source directory '%ls' does not exist.",
			sResolvedDirectory, sRuntimeSource);
		PrintError(L"pass --runtime-source <dir> pointing at a runtime10.0.10 directory.");
		return false;
	}

	if (bListOnly)
	{
		PrintLine(L"  C# runtime will be copied: %ls -> %ls", sRuntimeSource, sResolvedDirectory);
		return true;
	}

	PrintLine(L"  copying C# runtime: %ls -> %ls", sRuntimeSource, sResolvedDirectory);

	bool bAllSucceeded = true;
	const uint32 uCopiedCount = CopyDirectoryTree(
		sRuntimeSource, sResolvedDirectory, options.bVerbose, bAllSucceeded);
	PrintLine(L"  C# runtime copied (%d files).", uCopiedCount);
	return bAllSucceeded;
}

// Prints the whole staging plan without writing anything.
static void PrintStagingPlan(const BuildToolOptions& options)
{
	wchar_t sBinariesDirectory[k_nMaxPathLength] = {};
	swprintf_s(sBinariesDirectory, k_nMaxPathLength,
		L"%ls\\Engine\\Intermediate\\Binaries\\%ls_%ls",
		options.sRootPath, options.sConfiguration, options.sPlatform);

	PrintLine(L"VspBuildTool staging plan (no files written):");
	PrintLine(L"  repository    : %ls", options.sRootPath);
	PrintLine(L"  configuration : %ls", options.sConfiguration);
	PrintLine(L"  platform      : %ls", options.sPlatform);
	PrintLine(L"  run directory : %ls", options.sOutputDirectory);
	PrintLine(L"  binaries      : %ls", sBinariesDirectory);

	for (uint32 uIndex = 0; uIndex < k_nStageFileCount; ++uIndex)
	{
		const StageFileEntry& entry = k_sStageFiles[uIndex];
		wchar_t sSourcePath[k_nMaxPathLength] = {};
		const bool bHasSource = ResolveStageFileSource(
			options, sBinariesDirectory, entry, sSourcePath, k_nMaxPathLength);
		PrintLine(L"  [%ls] %ls", bHasSource ? L"stage" : L"skip ", entry.pFileName);
	}

	EnsureDotNetRuntime(options, true);
}

// -------------------------------------------------------------------------
// Command line
// -------------------------------------------------------------------------

static void PrintUsage()
{
	fwprintf(stdout,
		L"VspBuildTool - lightweight build system / stager for VspEngine\n"
		L"\n"
		L"Usage: VspBuildTool.exe [options]\n"
		L"\n"
		L"Options:\n"
		L"  --config <Debug|Release>   Configuration to stage (default: Debug).\n"
		L"  --platform <x64>           Platform to stage (default: x64).\n"
		L"  --build                    Invoke MSBuild on VspEngine.slnx first.\n"
		L"  --msbuild <path>           Explicit MSBuild.exe path (with --build).\n"
		L"  --root <dir>               Repository root (default: derived from the exe).\n"
		L"  --output <dir>             Run directory to stage into.\n"
		L"                             (default: <root>\\Engine\\Intermediate\\Binaries\\<config>_<platform>)\n"
		L"  --runtime-source <dir>     Source directory of the C# runtime (runtime10.0.10).\n"
		L"  --clean                    Delete the run directory before staging.\n"
		L"  --list                     Print the staging plan only (no writes).\n"
		L"  --verbose                  Log every copied file.\n"
		L"  --help                     Show this help.\n");
}

// Parses the command line into the options. Returns false when the arguments
// are invalid (the caller reports the error and shows the usage).
static bool ParseCommandLineOptions(int32 nArgumentCount, wchar_t** pArguments, BuildToolOptions& outOptions)
{
	auto ReadValueAfter = [](const wchar_t* pArgument, const wchar_t* pOptionName) -> const wchar_t*
	{
		const wchar_t* pMatch = wcsstr(pArgument, pOptionName);
		if (pMatch != pArgument)
		{
			return nullptr;
		}
		return pArgument + wcslen(pOptionName);
	};

	for (int32 nIndex = 1; nIndex < nArgumentCount; ++nIndex)
	{
		const wchar_t* pArgument = pArguments[nIndex];
		const wchar_t* pValue = nullptr;

		if (WideTextEqualsIgnoreCase(pArgument, L"--help") ||
			WideTextEqualsIgnoreCase(pArgument, L"-h"))
		{
			outOptions.bShowHelp = true;
			return true;
		}
		else if (WideTextEqualsIgnoreCase(pArgument, L"--build"))
		{
			outOptions.bBuildFirst = true;
		}
		else if (WideTextEqualsIgnoreCase(pArgument, L"--clean"))
		{
			outOptions.bCleanRunDirectory = true;
		}
		else if (WideTextEqualsIgnoreCase(pArgument, L"--list"))
		{
			outOptions.bListOnly = true;
		}
		else if (WideTextEqualsIgnoreCase(pArgument, L"--verbose"))
		{
			outOptions.bVerbose = true;
		}
		else if ((pValue = ReadValueAfter(pArgument, L"--config=")) != nullptr)
		{
			wcscpy_s(outOptions.sConfiguration, 32, pValue);
		}
		else if ((pValue = ReadValueAfter(pArgument, L"--platform=")) != nullptr)
		{
			wcscpy_s(outOptions.sPlatform, 32, pValue);
		}
		else if ((pValue = ReadValueAfter(pArgument, L"--msbuild=")) != nullptr)
		{
			wcscpy_s(outOptions.sMSBuildPath, k_nMaxPathLength, pValue);
		}
		else if ((pValue = ReadValueAfter(pArgument, L"--root=")) != nullptr)
		{
			wcscpy_s(outOptions.sRootPath, k_nMaxPathLength, pValue);
		}
		else if ((pValue = ReadValueAfter(pArgument, L"--output=")) != nullptr)
		{
			wcscpy_s(outOptions.sOutputDirectory, k_nMaxPathLength, pValue);
		}
		else if ((pValue = ReadValueAfter(pArgument, L"--runtime-source=")) != nullptr)
		{
			wcscpy_s(outOptions.sRuntimeSource, k_nMaxPathLength, pValue);
		}
		else if (WideTextEqualsIgnoreCase(pArgument, L"--config") ||
			WideTextEqualsIgnoreCase(pArgument, L"--platform") ||
			WideTextEqualsIgnoreCase(pArgument, L"--msbuild") ||
			WideTextEqualsIgnoreCase(pArgument, L"--root") ||
			WideTextEqualsIgnoreCase(pArgument, L"--output") ||
			WideTextEqualsIgnoreCase(pArgument, L"--runtime-source"))
		{
			if (nIndex + 1 >= nArgumentCount)
			{
				PrintError(L"option '%ls' expects a value.", pArgument);
				return false;
			}

			const wchar_t* pSpaceValue = pArguments[++nIndex];
			if (WideTextEqualsIgnoreCase(pArgument, L"--config"))
			{
				wcscpy_s(outOptions.sConfiguration, 32, pSpaceValue);
			}
			else if (WideTextEqualsIgnoreCase(pArgument, L"--platform"))
			{
				wcscpy_s(outOptions.sPlatform, 32, pSpaceValue);
			}
			else if (WideTextEqualsIgnoreCase(pArgument, L"--msbuild"))
			{
				wcscpy_s(outOptions.sMSBuildPath, k_nMaxPathLength, pSpaceValue);
			}
			else if (WideTextEqualsIgnoreCase(pArgument, L"--root"))
			{
				wcscpy_s(outOptions.sRootPath, k_nMaxPathLength, pSpaceValue);
			}
			else if (WideTextEqualsIgnoreCase(pArgument, L"--output"))
			{
				wcscpy_s(outOptions.sOutputDirectory, k_nMaxPathLength, pSpaceValue);
			}
			else if (WideTextEqualsIgnoreCase(pArgument, L"--runtime-source"))
			{
				wcscpy_s(outOptions.sRuntimeSource, k_nMaxPathLength, pSpaceValue);
			}
		}
		else
		{
			PrintError(L"unknown option: %ls", pArgument);
			return false;
		}
	}

	if (WideTextEqualsIgnoreCase(outOptions.sConfiguration, L"Debug") == false &&
		WideTextEqualsIgnoreCase(outOptions.sConfiguration, L"Release") == false)
	{
		PrintError(L"--config must be Debug or Release (got '%ls').", outOptions.sConfiguration);
		return false;
	}

	if (WideTextEqualsIgnoreCase(outOptions.sPlatform, L"x64") == false)
	{
		PrintError(L"--platform must be x64 (got '%ls').", outOptions.sPlatform);
		return false;
	}

	return true;
}

// Determines the repository root: the explicit option wins, then the
// executable's own location (four levels up from the binaries directory),
// then the current working directory. Returns false when no candidate holds
// the solution file.
static bool DetermineRepositoryRoot(BuildToolOptions& outOptions)
{
	if (outOptions.sRootPath[0] != L'\0')
	{
		wchar_t sSolutionPath[k_nMaxPathLength] = {};
		JoinPath(sSolutionPath, k_nMaxPathLength, outOptions.sRootPath, k_sSolutionFileName);
		if (FileExists(sSolutionPath))
		{
			return true;
		}
		PrintError(L"--root '%ls' does not contain %ls.", outOptions.sRootPath, k_sSolutionFileName);
		return false;
	}

	// Derive from the executable: <root>\Engine\Intermediate\Binaries\<cfg>_<platform>\VspBuildTool.exe
	wchar_t sExecutablePath[k_nMaxPathLength] = {};
#if VSP_PLATFORM_WINDOWS
	::GetModuleFileNameW(nullptr, sExecutablePath, k_nMaxPathLength);
#endif
	wchar_t sCandidateRoot[k_nMaxPathLength] = {};
	ResolveFullPath(sCandidateRoot, k_nMaxPathLength, sExecutablePath);

	for (uint32 uLevel = 0; uLevel < 6; ++uLevel)
	{
		// Strip the file name first, then walk parents.
		for (size_t nIndex = wcslen(sCandidateRoot); nIndex > 0; --nIndex)
		{
			if (sCandidateRoot[nIndex - 1] == k_cPathSeparator || sCandidateRoot[nIndex - 1] == L'/')
			{
				sCandidateRoot[nIndex - 1] = L'\0';
				break;
			}
		}

		wchar_t sSolutionPath[k_nMaxPathLength] = {};
		JoinPath(sSolutionPath, k_nMaxPathLength, sCandidateRoot, k_sSolutionFileName);
		if (FileExists(sSolutionPath))
		{
			wcscpy_s(outOptions.sRootPath, k_nMaxPathLength, sCandidateRoot);
			return true;
		}

		if (sCandidateRoot[0] == L'\0' || wcslen(sCandidateRoot) <= 3)
		{
			break;
		}
	}

	// Fall back to the current working directory.
	wchar_t sWorkingDirectory[k_nMaxPathLength] = {};
#if VSP_PLATFORM_WINDOWS
	::GetCurrentDirectoryW(k_nMaxPathLength, sWorkingDirectory);
#endif
	if (sWorkingDirectory[0] != L'\0')
	{
		wchar_t sSolutionPath[k_nMaxPathLength] = {};
		JoinPath(sSolutionPath, k_nMaxPathLength, sWorkingDirectory, k_sSolutionFileName);
		if (FileExists(sSolutionPath))
		{
			wcscpy_s(outOptions.sRootPath, k_nMaxPathLength, sWorkingDirectory);
			return true;
		}
	}

	PrintError(L"could not locate the repository root (no %ls found); pass --root <dir>.",
		k_sSolutionFileName);
	return false;
}

// -------------------------------------------------------------------------
// Entry point
// -------------------------------------------------------------------------

int wmain(int nArgumentCount, wchar_t** pArguments)
{
	BuildToolOptions options;
	if (!ParseCommandLineOptions(nArgumentCount, pArguments, options))
	{
		PrintUsage();
		return 1;
	}
	if (options.bShowHelp)
	{
		PrintUsage();
		return 0;
	}

	if (!DetermineRepositoryRoot(options))
	{
		return 1;
	}

	// Resolve the output directory (default: the canonical binaries dir).
	if (options.sOutputDirectory[0] == L'\0')
	{
		swprintf_s(options.sOutputDirectory, k_nMaxPathLength,
			L"%ls\\Engine\\Intermediate\\Binaries\\%ls_%ls",
			options.sRootPath, options.sConfiguration, options.sPlatform);
	}
	else
	{
		wchar_t sResolvedOutput[k_nMaxPathLength] = {};
		ResolveFullPath(sResolvedOutput, k_nMaxPathLength, options.sOutputDirectory);
		wcscpy_s(options.sOutputDirectory, k_nMaxPathLength, sResolvedOutput);
	}

	if (options.bListOnly)
	{
		PrintStagingPlan(options);
		return 0;
	}

	// 1. Optional build step.
	if (options.bBuildFirst)
	{
		wchar_t sMSBuildPath[k_nMaxPathLength] = {};
		if (!FindMSBuildExecutable(sMSBuildPath, k_nMaxPathLength, options))
		{
			PrintError(L"MSBuild.exe not found; pass --msbuild <path> or install Visual Studio.");
			return 1;
		}
		if (!InvokeMSBuild(sMSBuildPath, options))
		{
			return 1;
		}
	}

	// 2. Optional clean step.
	if (options.bCleanRunDirectory)
	{
		PrintLine(L"Cleaning run directory %ls", options.sOutputDirectory);
		RemoveDirectoryTree(options.sOutputDirectory);
	}

	// 3. Stage the runtime files.
	if (!EnsureDirectoryExists(options.sOutputDirectory))
	{
		PrintError(L"failed to create the run directory '%ls'.", options.sOutputDirectory);
		return 1;
	}
	if (!StageRuntimeFiles(options))
	{
		PrintError(L"staging failed.");
		return 1;
	}

	// 4. Make sure the C# runtime is reachable from the run directory.
	if (!EnsureDotNetRuntime(options, false))
	{
		PrintError(L"C# runtime staging failed.");
		return 1;
	}

	PrintLine(L"VspBuildTool finished. Run directory: %ls", options.sOutputDirectory);
	return 0;
}
