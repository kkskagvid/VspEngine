#include "RuntimePCH.h"

#include <cstdlib>

#include <Windows.h>

#include "Core/Logging/Log.h"
#include "Core/Templates/ArrayList.h"

namespace Vsp
{
	namespace LogDetail
	{
		static const char* const k_sLevelPrefixes[] =
		{
			"DEBUG", "INFO", "WARNING", "ERROR", "FATAL"
		};

		// Bounded history of the most recent log entries (the information
		// collected for the fatal crash report).
		static ArrayList<VspString> s_History;
		static uint32_t s_nHistoryLimit = Log::k_nDefaultHistoryLimit;

		// Registered backends. The list is replaceable at runtime; entries are
		// borrowed pointers that must outlive the log module.
		static ArrayList<LogBackend*> s_Backends;

		static bool s_bCrashPromptEnabled = true;

		// The Log module always starts with the debugger as its default
		// backend so log entries are never silently dropped; hosts can add or
		// replace backends afterwards.
		static DebugOutputDevice s_DefaultDebugDevice;
		static OutputDeviceLogBackend s_DefaultDebugBackend(&s_DefaultDebugDevice);

		struct DefaultBackendInstaller
		{
			DefaultBackendInstaller()
			{
				Log::AddBackend(&s_DefaultDebugBackend);
			}
		};
		static DefaultBackendInstaller s_DefaultBackendInstaller;

		const char* GetLevelPrefix(LogLevel eLevel)
		{
			const uint32_t nLevelIndex = static_cast<uint32_t>(eLevel);
			if (nLevelIndex >= 5)
			{
				return "???";
			}
			return k_sLevelPrefixes[nLevelIndex];
		}
	}

	using namespace LogDetail;

	// =========================================================================
	// Backends
	// =========================================================================

	void Log::AddBackend(LogBackend* pBackend)
	{
		if (pBackend == nullptr)
		{
			return;
		}

		for (size_t nIndex = 0; nIndex < s_Backends.GetSize(); ++nIndex)
		{
			if (s_Backends[nIndex] == pBackend)
			{
				return;   // Already registered.
			}
		}

		s_Backends.Add(pBackend);
	}

	void Log::RemoveBackend(LogBackend* pBackend)
	{
		for (size_t nIndex = 0; nIndex < s_Backends.GetSize(); ++nIndex)
		{
			if (s_Backends[nIndex] == pBackend)
			{
				s_Backends.RemoveAt(nIndex);
				return;
			}
		}
	}

	void Log::ClearBackends()
	{
		s_Backends.Clear();
	}

	void Log::Flush()
	{
		for (size_t nIndex = 0; nIndex < s_Backends.GetSize(); ++nIndex)
		{
			s_Backends[nIndex]->Flush();
		}
	}

	// =========================================================================
	// History
	// =========================================================================

	void Log::SetHistoryLimit(uint32_t uMaxEntryCount)
	{
		s_nHistoryLimit = uMaxEntryCount;

		// Trim the existing history down to the new limit.
		while (s_History.GetSize() > s_nHistoryLimit)
		{
			s_History.RemoveAt(0);
		}
	}

	uint32_t Log::GetHistoryLimit()
	{
		return s_nHistoryLimit;
	}

	uint32_t Log::GetHistoryCount()
	{
		return static_cast<uint32_t>(s_History.GetSize());
	}

	void Log::CollectHistory(VspString& outHistoryText)
	{
		outHistoryText = nullptr;
		for (size_t nIndex = 0; nIndex < s_History.GetSize(); ++nIndex)
		{
			outHistoryText.Append(s_History[nIndex]);
			outHistoryText.Append("\n");
		}
	}

	void Log::AppendToHistory(const VspString& sLine)
	{
		if (s_nHistoryLimit == 0)
		{
			return;
		}

		while (s_History.GetSize() >= s_nHistoryLimit)
		{
			s_History.RemoveAt(0);
		}
		s_History.Add(sLine);
	}

	// =========================================================================
	// Crash prompt
	// =========================================================================

	void Log::SetCrashPromptEnabled(bool bEnabled)
	{
		s_bCrashPromptEnabled = bEnabled;
	}

	bool Log::IsCrashPromptEnabled()
	{
		return s_bCrashPromptEnabled;
	}

	void Log::PresentCrashReport(const VspString& sFatalLine, const VspString& sHistoryText)
	{
		VspString sReport;
		sReport.Append("A fatal error occurred and the engine will terminate.\n");
		sReport.Append("========================================\n");
		sReport.Append(sFatalLine);
		sReport.Append("\n========================================\n");
		sReport.Append("Collected log history:\n");
		sReport.Append(sHistoryText);
		sReport.Append("========================================\n");

		// Prompt the user with the collected information, then crash.
		MessageBoxA(
			nullptr,
			sReport.GetData(),
			"Vsp Engine - Fatal Error",
			MB_OK | MB_ICONERROR);

		std::abort();
	}

	// =========================================================================
	// Writing
	// =========================================================================

	void Log::Write(LogLevel eLevel, const char* pTag, const VspString& sMessage)
	{
		const uint64_t uElapsedMilliseconds = GetTickCount64();
		const char* pLevelPrefix = GetLevelPrefix(eLevel);

		VspString sLine;
		if (pTag != nullptr && pTag[0] != '\0')
		{
			sLine = VspFormat::Format("[{}] [{} ms] [{}] {}", pLevelPrefix, uElapsedMilliseconds, pTag, sMessage);
		}
		else
		{
			sLine = VspFormat::Format("[{}] [{} ms] {}", pLevelPrefix, uElapsedMilliseconds, sMessage);
		}

		// Collect the line for the crash report before anything else.
		AppendToHistory(sLine);

		// Forward to every registered backend.
		for (size_t nIndex = 0; nIndex < s_Backends.GetSize(); ++nIndex)
		{
			s_Backends[nIndex]->WriteLogEntry(eLevel, pTag, sLine);
		}

		if (eLevel != LogLevel::Fatal)
		{
			return;
		}

		// Fatal: collect the gathered information and prompt the crash.
		VspString sHistoryText;
		CollectHistory(sHistoryText);
		Flush();

		if (s_bCrashPromptEnabled)
		{
			PresentCrashReport(sLine, sHistoryText);
		}
		else
		{
			// Prompts disabled (automation): report to the backends only.
			const VspString sReportLine = VspFormat::Format(
				"[FTL] Crash report follows (prompt disabled).\n{}", sHistoryText);
			for (size_t nIndex = 0; nIndex < s_Backends.GetSize(); ++nIndex)
			{
				s_Backends[nIndex]->WriteLogEntry(eLevel, pTag, sReportLine);
			}
		}
	}
}
