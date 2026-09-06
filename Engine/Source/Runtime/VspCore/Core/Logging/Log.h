#pragma once

#include "Core/Core.h"
#include "Core/Logging/LogBackend.h"
#include "Core/String/VspString.h"
#include "Core/String/VspStringFormat.h"

namespace Vsp
{
	// -------------------------------------------------------------------------
	// Log
	// -------------------------------------------------------------------------
	// Engine logging facade. Every entry is formatted once (with VspFormat)
	// and forwarded to the registered backends, so the output target is fully
	// replaceable: AddBackend / RemoveBackend / ClearBackends.
	//
	// The facade also keeps a bounded history of recent entries. When a Fatal
	// entry is written, the history is collected into a crash report and a
	// crash prompt is shown (unless prompts are disabled), after which the
	// process aborts. The engine never throws.
	// -------------------------------------------------------------------------
	class RUNTIME_API Log
	{
	public:
		static constexpr uint32 k_nDefaultHistoryLimit = 64;

		// -------- Backends (pluggable) --------
		static void AddBackend(LogBackend* pBackend);
		static void RemoveBackend(LogBackend* pBackend);
		static void ClearBackends();

		// -------- Collected log history (used by the fatal crash report) --------
		static void SetHistoryLimit(uint32 uMaxEntryCount);
		static uint32 GetHistoryLimit();
		static uint32 GetHistoryCount();
		static void CollectHistory(VspString& outHistoryText);

		// -------- Crash prompt --------
		static void SetCrashPromptEnabled(bool bEnabled);
		static bool IsCrashPromptEnabled();

		// -------- Writing --------
		static void Write(LogLevel eLevel, const char* pTag, const VspString& sMessage);
		static void Flush();

	private:
		static void AppendToHistory(const VspString& sLine);
		static void PresentCrashReport(const VspString& sFatalLine, const VspString& sHistoryText);
	};
}

// Usage: LOG_INFO(kLogTag, "message with {}", value);
#define LOG_DEBUG(Tag, ...)   ::Vsp::Log::Write(::Vsp::LogLevel::Debug,   Tag, ::Vsp::VspFormat::Format(__VA_ARGS__))
#define LOG_INFO(Tag, ...)    ::Vsp::Log::Write(::Vsp::LogLevel::Info,    Tag, ::Vsp::VspFormat::Format(__VA_ARGS__))
#define LOG_WARNING(Tag, ...) ::Vsp::Log::Write(::Vsp::LogLevel::Warning, Tag, ::Vsp::VspFormat::Format(__VA_ARGS__))
#define LOG_ERROR(Tag, ...)   ::Vsp::Log::Write(::Vsp::LogLevel::Error,   Tag, ::Vsp::VspFormat::Format(__VA_ARGS__))
#define LOG_FATAL(Tag, ...)   ::Vsp::Log::Write(::Vsp::LogLevel::Fatal,   Tag, ::Vsp::VspFormat::Format(__VA_ARGS__))
