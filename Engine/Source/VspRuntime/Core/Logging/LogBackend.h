#pragma once

#include "Core/Core.h"
#include "Core/Output/OutputDevice.h"
#include "Core/String/VspString.h"

namespace Vsp
{
	enum class LogLevel : uint8_t
	{
		Debug,
		Info,
		Warning,
		Error,
		Fatal,
	};

	// -------------------------------------------------------------------------
	// LogBackend
	// -------------------------------------------------------------------------
	// Pluggable logging backend. The Log facade formats every entry once and
	// forwards the resulting line to every registered backend; swapping the
	// output target therefore only means swapping backends. Implementations
	// must never throw.
	// -------------------------------------------------------------------------
	class LogBackend
	{
	public:
		virtual ~LogBackend() = default;

		// Receives one fully formatted line (without a trailing newline).
		virtual void WriteLogEntry(LogLevel eLevel, const char* pTag, const VspString& sLine) = 0;

		virtual void Flush() {}
	};

	// -------------------------------------------------------------------------
	// OutputDeviceLogBackend
	// -------------------------------------------------------------------------
	// Adapts any OutputDevice (Debug, Console, File, ...) into a logging
	// backend. The device pointer is borrowed: it must outlive the backend.
	// -------------------------------------------------------------------------
	class OutputDeviceLogBackend : public LogBackend
	{
	public:
		explicit OutputDeviceLogBackend(OutputDevice* pDevice)
			: m_pDevice(pDevice)
		{
		}

		void WriteLogEntry(LogLevel eLevel, const char* pTag, const VspString& sLine) override
		{
			if (m_pDevice == nullptr || !m_pDevice->IsOpen())
			{
				return;
			}

			m_pDevice->Write(sLine);
			m_pDevice->Write(VspString("\n"));
		}

		void Flush() override
		{
			if (m_pDevice != nullptr)
			{
				m_pDevice->Flush();
			}
		}

	private:
		OutputDevice* m_pDevice = nullptr;
	};
}
