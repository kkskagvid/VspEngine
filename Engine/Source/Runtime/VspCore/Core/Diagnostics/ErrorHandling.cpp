#include "RuntimePCH.h"

#include "Common/PlatformMisc.h"
#include "Core/Diagnostics/ErrorHandling.h"
#include "Core/Logging/Log.h"

namespace Vsp
{
	static constexpr const char* kLogTag = "ProcessFailedExit";

	void ProcessFailedExit(const char* pTag, const VspString& sReason)
	{
		// 1. The fatal entry. Log::Write collects the recent history into a
		//    crash report and forwards it to every registered backend, so the
		//    log file (or the debugger) has the reason AND what led to it.
		Log::Write(LogLevel::Fatal, pTag, sReason);

		// 2. Make sure what was just written really left the process.
		Log::Flush();

		// 3. The crash prompt, unless it was switched off (automation runs use
		//    --silent, where a modal dialog would block a headless run).
		if (Log::IsCrashPromptEnabled())
		{
			VspString sReport;
			sReport.Append("A fatal error occurred and the engine will terminate.\n");
			sReport.Append("========================================\n");
			sReport.Append(sReason);
			sReport.Append("\n========================================\n");

			VspString sHistoryText;
			Log::CollectHistory(sHistoryText);
			sReport.Append("Collected log history:\n");
			sReport.Append(sHistoryText);
			sReport.Append("========================================\n");

			PlatformMisc::ShowErrorPrompt("Vsp Engine - Fatal Error", sReport.GetData());
		}
		else
		{
			// Prompts are off: the backends have the whole report already, but
			// a reader of a console log should still see where it stopped.
			LOG_ERROR(kLogTag, "Process terminates here: '{}' is fatal.", pTag != nullptr ? pTag : "");
		}

		// 4. Terminate. The exit code is non-zero so a scheduled run (and the
		//    acceptance script that watches it) sees the failure.
		PlatformMisc::TerminateProcess(1);

		// PlatformMisc::TerminateProcess never returns; the loop keeps the
		// [[noreturn]] contract provable to the compiler even if a future
		// implementation of it does return.
		while (true)
		{
			PlatformMisc::TerminateProcess(1);
		}
	}

	void ProcessFailedExit(const char* pTag, const char* pReasonUtf8)
	{
		ProcessFailedExit(pTag, VspString(pReasonUtf8));
	}
}
