#include "RuntimePCH.h"

#include <cstdlib>
#include <mutex>

#include "Common/PlatformMisc.h"
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

		const char* GetLevelPrefix(LogLevel eLevel)
		{
			const uint32 nLevelIndex = static_cast<uint32>(eLevel);
			if (nLevelIndex >= 5)
			{
				return "???";
			}
			return k_sLevelPrefixes[nLevelIndex];
		}

		// ---------------------------------------------------------------------
		// The log's state, behind one accessor
		// ---------------------------------------------------------------------
		// A log entry can be written from ANOTHER translation unit's static
		// initializer - before a namespace-scope static in this file would have
		// been constructed - so the state is built on first use rather than at
		// load time. The same accessor is what makes the state reachable by every
		// function below, which is what the single lock protects.
		//
		// The lock is RECURSIVE because the facade's functions call each other (a
		// Fatal entry collects the history, which collects the crash report), and
		// a plain mutex would deadlock on the second frame.
		// ---------------------------------------------------------------------
		struct LogState
		{
			std::recursive_mutex Mutex;

			// Bounded history of the most recent log entries (the information the
			// fatal crash report collects).
			ArrayList<VspString> History;
			uint32 nHistoryLimit = Log::k_nDefaultHistoryLimit;

			// Registered backends. The list is replaceable at runtime; entries are
			// borrowed pointers that must outlive the log module.
			ArrayList<LogBackend*> Backends;

			bool bCrashPromptEnabled = true;

			// The debugger is the backend the log always has: the state owns it, and
			// Write installs it when nothing else is registered, so an entry written
			// before a host wired its own backends is not dropped.
			DebugOutputDevice DefaultDebugDevice;
			OutputDeviceLogBackend DefaultDebugBackend{ &DefaultDebugDevice };

			// True once a host asked for no backends at all (ClearBackends): the
			// default is then not re-installed behind its back.
			bool bDefaultBackendDisabled = false;
		};

		LogState& GetLogState()
		{
			static LogState s_State;
			return s_State;
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

		LogState& state = GetLogState();
		std::lock_guard<std::recursive_mutex> lock(state.Mutex);

		for (size_t nIndex = 0; nIndex < state.Backends.GetSize(); ++nIndex)
		{
			if (state.Backends[nIndex] == pBackend)
			{
				return;   // Already registered.
			}
		}

		state.Backends.Add(pBackend);
	}

	void Log::RemoveBackend(LogBackend* pBackend)
	{
		LogState& state = GetLogState();
		std::lock_guard<std::recursive_mutex> lock(state.Mutex);

		for (size_t nIndex = 0; nIndex < state.Backends.GetSize(); ++nIndex)
		{
			if (state.Backends[nIndex] == pBackend)
			{
				state.Backends.RemoveAt(nIndex);
				return;
			}
		}
	}

	void Log::ClearBackends()
	{
		LogState& state = GetLogState();
		std::lock_guard<std::recursive_mutex> lock(state.Mutex);

		state.Backends.Clear();
		state.bDefaultBackendDisabled = true;
	}

	void Log::Flush()
	{
		LogState& state = GetLogState();
		std::lock_guard<std::recursive_mutex> lock(state.Mutex);

		for (size_t nIndex = 0; nIndex < state.Backends.GetSize(); ++nIndex)
		{
			state.Backends[nIndex]->Flush();
		}
	}

	// =========================================================================
	// History
	// =========================================================================

	void Log::SetHistoryLimit(uint32 uMaxEntryCount)
	{
		LogState& state = GetLogState();
		std::lock_guard<std::recursive_mutex> lock(state.Mutex);

		state.nHistoryLimit = uMaxEntryCount;

		// Trim the existing history down to the new limit.
		while (state.History.GetSize() > state.nHistoryLimit)
		{
			state.History.RemoveAt(0);
		}
	}

	uint32 Log::GetHistoryLimit()
	{
		LogState& state = GetLogState();
		std::lock_guard<std::recursive_mutex> lock(state.Mutex);
		return state.nHistoryLimit;
	}

	uint32 Log::GetHistoryCount()
	{
		LogState& state = GetLogState();
		std::lock_guard<std::recursive_mutex> lock(state.Mutex);
		return static_cast<uint32>(state.History.GetSize());
	}

	void Log::CollectHistory(VspString& outHistoryText)
	{
		LogState& state = GetLogState();
		std::lock_guard<std::recursive_mutex> lock(state.Mutex);

		outHistoryText = nullptr;
		for (size_t nIndex = 0; nIndex < state.History.GetSize(); ++nIndex)
		{
			outHistoryText.Append(state.History[nIndex]);
			outHistoryText.Append("\n");
		}
	}

	// Called with the lock already held (from Write).
	void Log::AppendToHistory(const VspString& sLine)
	{
		LogState& state = GetLogState();
		if (state.nHistoryLimit == 0)
		{
			return;
		}

		while (state.History.GetSize() >= state.nHistoryLimit)
		{
			state.History.RemoveAt(0);
		}
		state.History.Add(sLine);
	}

	// =========================================================================
	// Crash prompt
	// =========================================================================

	void Log::SetCrashPromptEnabled(bool bEnabled)
	{
		LogState& state = GetLogState();
		std::lock_guard<std::recursive_mutex> lock(state.Mutex);
		state.bCrashPromptEnabled = bEnabled;
	}

	bool Log::IsCrashPromptEnabled()
	{
		LogState& state = GetLogState();
		std::lock_guard<std::recursive_mutex> lock(state.Mutex);
		return state.bCrashPromptEnabled;
	}

	void Log::CollectCrashReport(const VspString& sReason, VspString& outReportText)
	{
		VspString sHistoryText;
		CollectHistory(sHistoryText);

		outReportText = nullptr;
		outReportText.Append("A fatal error occurred and the engine will terminate.\n");
		outReportText.Append("========================================\n");
		outReportText.Append(sReason);
		outReportText.Append("\n========================================\n");
		outReportText.Append("Collected log history:\n");
		outReportText.Append(sHistoryText);
		outReportText.Append("========================================\n");
	}

	// =========================================================================
	// Writing
	// =========================================================================

	void Log::Write(LogLevel eLevel, const char* pTag, const VspString& sMessage)
	{
		LogState& state = GetLogState();
		std::lock_guard<std::recursive_mutex> lock(state.Mutex);

		// Never drop an entry for want of a backend: a host that has not wired one
		// yet - or an entry written from another translation unit's static
		// initializer - still reaches the debugger.
		if (state.Backends.GetSize() == 0 && !state.bDefaultBackendDisabled)
		{
			state.Backends.Add(&state.DefaultDebugBackend);
		}

		const uint64_t uElapsedMilliseconds = PlatformMisc::GetElapsedMilliseconds();
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
		for (size_t nIndex = 0; nIndex < state.Backends.GetSize(); ++nIndex)
		{
			state.Backends[nIndex]->WriteLogEntry(eLevel, pTag, sLine);
		}

		if (eLevel != LogLevel::Fatal)
		{
			return;
		}

		// Fatal: the reason alone is not enough to debug, so the recent history
		// travels with it. Terminating the process is ProcessFailedExit's job
		// (Core/Diagnostics/ErrorHandling.h), not the log's.
		VspString sReportText;
		CollectCrashReport(sMessage, sReportText);

		for (size_t nIndex = 0; nIndex < state.Backends.GetSize(); ++nIndex)
		{
			state.Backends[nIndex]->Flush();
			state.Backends[nIndex]->WriteLogEntry(eLevel, pTag, sReportText);
		}
	}
}
