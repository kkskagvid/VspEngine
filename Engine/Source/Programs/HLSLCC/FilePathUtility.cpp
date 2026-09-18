#include "FilePathUtility.h"

#include <cstdio>
#include <cstdlib>

#if defined(_WIN32)
	#define WIN32_LEAN_AND_MEAN
	#include <Windows.h>
#else
	#include <sys/stat.h>
#endif

namespace Hlslcc
{
	std::string FilePathUtility::GetExecutableDirectory()
	{
#if defined(_WIN32)
		char sExecutablePath[2048] = {};
		const DWORD nLength = ::GetModuleFileNameA(nullptr, sExecutablePath, sizeof(sExecutablePath));
		if (nLength == 0)
		{
			return std::string();
		}

		std::string sExecutableDirectory(sExecutablePath);
		const size_t nSeparatorOffset = sExecutableDirectory.find_last_of("\\\\/");
		if (nSeparatorOffset != std::string::npos)
		{
			sExecutableDirectory.resize(nSeparatorOffset);
		}
		return sExecutableDirectory;
#else
		return std::string();
#endif
	}

	std::string FilePathUtility::Combine(const std::string& sLeftPath, const std::string& sRightPath)
	{
		if (sLeftPath.empty())
		{
			return sRightPath;
		}
		if (sRightPath.empty())
		{
			return sLeftPath;
		}

		std::string sCombined = sLeftPath;
		const char cLastCharacter = sCombined[sCombined.size() - 1];
		if (cLastCharacter != '\\' && cLastCharacter != '/')
		{
			sCombined.push_back('\\');
		}
		sCombined += sRightPath;
		return sCombined;
	}

	std::string FilePathUtility::GetAbsolutePath(const std::string& sFilePath)
	{
		if (sFilePath.empty())
		{
			return sFilePath;
		}

#if defined(_WIN32)
		char sResolvedPath[2048] = {};
		const DWORD nLength = ::GetFullPathNameA(sFilePath.c_str(), sizeof(sResolvedPath), sResolvedPath, nullptr);
		if (nLength == 0 || nLength >= sizeof(sResolvedPath))
		{
			return sFilePath;
		}
		return std::string(sResolvedPath);
#else
		return sFilePath;
#endif
	}

	bool FilePathUtility::DoesFileExist(const std::string& sFilePath)
	{
		if (sFilePath.empty())
		{
			return false;
		}

#if defined(_WIN32)
		const DWORD nAttributes = ::GetFileAttributesA(sFilePath.c_str());
		return nAttributes != INVALID_FILE_ATTRIBUTES && (nAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
#else
		std::FILE* pFile = std::fopen(sFilePath.c_str(), "rb");
		if (pFile == nullptr)
		{
			return false;
		}
		std::fclose(pFile);
		return true;
#endif
	}

	bool FilePathUtility::DoesDirectoryExist(const std::string& sDirectoryPath)
	{
		if (sDirectoryPath.empty())
		{
			return false;
		}

#if defined(_WIN32)
		const DWORD nAttributes = ::GetFileAttributesA(sDirectoryPath.c_str());
		return nAttributes != INVALID_FILE_ATTRIBUTES && (nAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
		return DoesFileExist(sDirectoryPath);
#endif
	}

	bool FilePathUtility::EnsureDirectoryExists(const std::string& sDirectoryPath)
	{
		if (sDirectoryPath.empty() || DoesDirectoryExist(sDirectoryPath))
		{
			return !sDirectoryPath.empty();
		}

		// Create the parents first so a nested output path works in one call.
		const size_t nSeparatorOffset = sDirectoryPath.find_last_of("\\\\/");
		if (nSeparatorOffset != std::string::npos && nSeparatorOffset > 0)
		{
			const std::string sParentDirectory = sDirectoryPath.substr(0, nSeparatorOffset);
			if (!sParentDirectory.empty() && sParentDirectory != "." && !DoesDirectoryExist(sParentDirectory))
			{
				EnsureDirectoryExists(sParentDirectory);
			}
		}

#if defined(_WIN32)
		::CreateDirectoryA(sDirectoryPath.c_str(), nullptr);
#else
		mkdir(sDirectoryPath.c_str(), 0755);
#endif
		return DoesDirectoryExist(sDirectoryPath);
	}

	std::string FilePathUtility::GetEnvironmentValue(const char* pVariableName)
	{
		if (pVariableName == nullptr)
		{
			return std::string();
		}

		const char* pValue = std::getenv(pVariableName);
		return (pValue != nullptr) ? std::string(pValue) : std::string();
	}

	std::string FilePathUtility::GetFileName(const std::string& sFilePath)
	{
		const size_t nSeparatorOffset = sFilePath.find_last_of("\\\\/");
		return (nSeparatorOffset == std::string::npos) ? sFilePath : sFilePath.substr(nSeparatorOffset + 1);
	}

	std::string FilePathUtility::GetFileBaseName(const std::string& sFilePath)
	{
		const std::string sFileName = GetFileName(sFilePath);
		const size_t nExtensionOffset = sFileName.find_last_of('.');
		return (nExtensionOffset == std::string::npos || nExtensionOffset == 0)
			? sFileName
			: sFileName.substr(0, nExtensionOffset);
	}

	std::string FilePathUtility::GetDirectory(const std::string& sFilePath)
	{
		const size_t nSeparatorOffset = sFilePath.find_last_of("\\\\/");
		return (nSeparatorOffset == std::string::npos) ? std::string(".") : sFilePath.substr(0, nSeparatorOffset);
	}
}
