#include "RuntimePCH.h"

#include <cstdio>

#include <Windows.h>

#include "Core/Logging/Log.h"

namespace Vsp
{
	namespace LogDetail
	{
		static const char* const k_sLevelPrefixes[] =
		{
			"DEBUG", "INFO", "WARNING", "ERROR", "FATAL"
		};

		static std::string s_LogFilePath;

		void SetLogFilePath(const char* pUtf8FilePath)
		{
			s_LogFilePath = (pUtf8FilePath != nullptr) ? pUtf8FilePath : "";
		}

		void WriteLog(LogLevel eLevel, const char* pTag, const std::string& sMessage)
		{
			const uint32_t nLevelIndex = static_cast<uint32_t>(eLevel);
			const char* pLevelPrefix = (nLevelIndex < 5) ? k_sLevelPrefixes[nLevelIndex] : "???";

			// Milliseconds since the process started, so frame pacing is visible.
			const uint64_t uElapsedMilliseconds = GetTickCount64();

			char sLineBuffer[4096];
			int nWrittenCount = 0;
			if (pTag != nullptr && pTag[0] != '\0')
			{
				nWrittenCount = snprintf(sLineBuffer, sizeof(sLineBuffer),
					"[%s] [%llu ms] [%s] %s\n", pLevelPrefix,
					static_cast<unsigned long long>(uElapsedMilliseconds), pTag, sMessage.c_str());
			}
			else
			{
				nWrittenCount = snprintf(sLineBuffer, sizeof(sLineBuffer),
					"[%s] [%llu ms] %s\n", pLevelPrefix,
					static_cast<unsigned long long>(uElapsedMilliseconds), sMessage.c_str());
			}
			if (nWrittenCount < 0)
			{
				return;
			}

			OutputDebugStringA(sLineBuffer);

			if (!s_LogFilePath.empty())
			{
				FILE* pFile = nullptr;
				fopen_s(&pFile, s_LogFilePath.c_str(), "ab");
				if (pFile != nullptr)
				{
					fwrite(sLineBuffer, 1, static_cast<size_t>(nWrittenCount), pFile);
					fclose(pFile);
				}
			}
		}
	}
}
