#pragma once

#include <string>
#include <utility>

// Header-only fmt so every consumer (runtime DLL and host exe alike) can use
// the logging macros without linking a separate fmt library.
#ifndef FMT_HEADER_ONLY
	#define FMT_HEADER_ONLY
#endif

#include <fmt/format.h>

#include "Core/Core.h"

namespace Vsp
{
	namespace LogDetail
	{
		enum class LogLevel : uint8_t
		{
			Debug,
			Info,
			Warning,
			Error,
			Fatal
		};

		// Writes one formatted log line. The engine never throws: invalid
		// format strings abort the process (fmt is compiled without exceptions).
		RUNTIME_API void WriteLog(LogLevel eLevel, const char* pTag, const std::string& sMessage);

		// Directs log output to a UTF-8 file path (in addition to the debugger).
		RUNTIME_API void SetLogFilePath(const char* pUtf8FilePath);

		template <typename... ArgumentTypes>
		std::string FormatLogMessage(fmt::format_string<ArgumentTypes...> format, ArgumentTypes&&... arguments)
		{
			return fmt::format(format, std::forward<ArgumentTypes>(arguments)...);
		}
	}
}

// Usage: LOG_INFO(kLogTag, "message with {}", value);
#define LOG_DEBUG(Tag, ...)   ::Vsp::LogDetail::WriteLog(::Vsp::LogDetail::LogLevel::Debug,   Tag, ::Vsp::LogDetail::FormatLogMessage(__VA_ARGS__))
#define LOG_INFO(Tag, ...)    ::Vsp::LogDetail::WriteLog(::Vsp::LogDetail::LogLevel::Info,    Tag, ::Vsp::LogDetail::FormatLogMessage(__VA_ARGS__))
#define LOG_WARNING(Tag, ...) ::Vsp::LogDetail::WriteLog(::Vsp::LogDetail::LogLevel::Warning, Tag, ::Vsp::LogDetail::FormatLogMessage(__VA_ARGS__))
#define LOG_ERROR(Tag, ...)   ::Vsp::LogDetail::WriteLog(::Vsp::LogDetail::LogLevel::Error,   Tag, ::Vsp::LogDetail::FormatLogMessage(__VA_ARGS__))
#define LOG_FATAL(Tag, ...)   ::Vsp::LogDetail::WriteLog(::Vsp::LogDetail::LogLevel::Fatal,   Tag, ::Vsp::LogDetail::FormatLogMessage(__VA_ARGS__))
