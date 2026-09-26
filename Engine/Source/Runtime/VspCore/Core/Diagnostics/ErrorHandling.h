#pragma once

#include "Core/Core.h"
#include "Core/Logging/Log.h"
#include "Core/String/VspString.h"
#include "Core/String/VspStringFormat.h"

// -------------------------------------------------------------------------
// The engine's error handling rule
// -------------------------------------------------------------------------
// The runtime never throws (the whole DLL is built with /EHs-c- and
// _HAS_EXCEPTIONS=0), so every failure travels through a return value. Two
// kinds of failure exist, and each has exactly ONE prescribed reaction:
//
//   NON-FATAL - the operation failed, the engine can keep running.
//       * function returns a value  -> log the reason, then return the EMPTY
//         value: nullptr, 0, false, an invalid handle, an empty VspString.
//       * function returns nothing  -> log the reason, then return.
//     Use VSP_RETURN_EMPTY / VSP_RETURN_VOID (or write the two steps out by
//     hand; the macros only exist so the rule stays enforceable by reading).
//
//   FATAL - the engine cannot continue (no window, no graphics device, no
//     managed runtime, a corrupt shader container, an allocation the engine
//     needs to make progress).
//     -> log the reason, then call ProcessFailedExit(), which reports the
//        collected log history and terminates the process with a non-zero
//        exit code. A fatal path never returns.
//     Use VSP_FATAL_ERROR.
//
// Logging convenience wrappers for the non-fatal case live here too, so a
// failure path reads the same everywhere:
//
//     if (!CreateDevice())
//     {
//         VSP_RETURN_EMPTY(false, kLogTag, "the device could not be created");
//     }
//
//     if (pBuffer == nullptr)
//     {
//         VSP_RETURN_VOID(kLogTag, "the buffer was null");
//     }
// -------------------------------------------------------------------------

namespace Vsp
{
	// -------------------------------------------------------------------------
	// ProcessFailedExit
	// -------------------------------------------------------------------------
	// The engine's fatal-error exit path. A fatal error is one the engine cannot
	// continue from; it is reported by
	//
	//   1. writing a Fatal entry into the log (which collects the recent log
	//      history into a crash report),
	//   2. flushing every backend, so the message has reached the log file
	//      before the process goes away,
	//   3. showing the crash prompt when prompts are enabled,
	//   4. terminating the process with a non-zero exit code.
	//
	// The function never returns; every call site can be read as "the engine
	// stops here". It is declared [[noreturn]] so the compiler agrees.
	// -------------------------------------------------------------------------
	[[noreturn]] void ProcessFailedExit(const char* pTag, const VspString& sReason);

	// Convenience overload so a literal reason needs no VspString around it.
	[[noreturn]] void ProcessFailedExit(const char* pTag, const char* pReasonUtf8);
}

// Logs at Error level. Used for a non-fatal failure that the caller reports
// and then handles itself (a retry, a fallback, a degraded mode).
#define VSP_LOG_ERROR(Tag, ...) \
	::Vsp::Log::Write(::Vsp::LogLevel::Error, Tag, ::Vsp::VspFormat::Format(__VA_ARGS__))

// Logs the reason and terminates the process. Fatal errors only.
#define VSP_FATAL_ERROR(Tag, ...) \
	::Vsp::ProcessFailedExit(Tag, ::Vsp::VspFormat::Format(__VA_ARGS__))

// Logs the reason and returns EmptyValue from the enclosing function.
#define VSP_RETURN_EMPTY(EmptyValue, Tag, ...) \
	do { VSP_LOG_ERROR(Tag, __VA_ARGS__); return (EmptyValue); } while (false)

// Logs the reason and returns from the enclosing void function.
#define VSP_RETURN_VOID(Tag, ...) \
	do { VSP_LOG_ERROR(Tag, __VA_ARGS__); return; } while (false)
