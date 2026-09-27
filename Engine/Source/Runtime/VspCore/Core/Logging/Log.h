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
	// entry is written, the reason and the history are collected into a crash
	// report and forwarded to every backend, so the log file says what happened
	// and what led to it.
	//
	// Log does NOT terminate the process: killing the engine is the job of
	// ProcessFailedExit (Core/Diagnostics/ErrorHandling.h), which writes the
	// fatal entry, flushes, shows the crash prompt when prompts are enabled and
	// then exits with a non-zero code. The engine never throws.
	//
	// Thread safety: the facade's state (the backends, the history, the crash
	// prompt flag) lives behind one recursive lock, so a line written from another
	// thread is serialized rather than raced - which matters because a backend
	// writes to a file, and two threads interleaving bytes into one log file is
	// corruption. That is the whole of the module's thread contract: the engine
	// still logs from the thread that runs the frame, and a line's ORDER is the
	// order the lock was taken in.
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

		// Builds the crash report a fatal error is presented with: the reason
		// followed by the collected log history. ProcessFailedExit uses it.
		static void CollectCrashReport(const VspString& sReason, VspString& outReportText);

		// -------- Writing --------
		static void Write(LogLevel eLevel, const char* pTag, const VspString& sMessage);
		static void Flush();

	private:
		static void AppendToHistory(const VspString& sLine);
	};
}

// Usage: LOG_INFO(kLogTag, "message with {}", value);
#define LOG_DEBUG(Tag, ...)   ::Vsp::Log::Write(::Vsp::LogLevel::Debug,   Tag, ::Vsp::VspFormat::Format(__VA_ARGS__))
#define LOG_INFO(Tag, ...)    ::Vsp::Log::Write(::Vsp::LogLevel::Info,    Tag, ::Vsp::VspFormat::Format(__VA_ARGS__))
#define LOG_WARNING(Tag, ...) ::Vsp::Log::Write(::Vsp::LogLevel::Warning, Tag, ::Vsp::VspFormat::Format(__VA_ARGS__))
#define LOG_ERROR(Tag, ...)   ::Vsp::Log::Write(::Vsp::LogLevel::Error,   Tag, ::Vsp::VspFormat::Format(__VA_ARGS__))
#define LOG_FATAL(Tag, ...)   ::Vsp::Log::Write(::Vsp::LogLevel::Fatal,   Tag, ::Vsp::VspFormat::Format(__VA_ARGS__))
