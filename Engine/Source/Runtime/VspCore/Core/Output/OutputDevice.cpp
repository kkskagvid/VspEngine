#include "RuntimePCH.h"

#include <cstdio>

#include "Common/PlatformMisc.h"
#include "Core/EngineServices.h"
#include "Core/Logging/Log.h"
#include "Core/Output/OutputDevice.h"

namespace Vsp
{
	static constexpr const char* kLogTag = "OutputDevice";

	// =========================================================================
	// DebugOutputDevice
	// =========================================================================

	void DebugOutputDevice::Write(const VspString& sContent)
	{
		PlatformMisc::WriteToDebugOutput(sContent.GetData());
	}

	// =========================================================================
	// ConsoleOutputDevice
	// =========================================================================

	void ConsoleOutputDevice::Write(const VspString& sContent)
	{
		fwrite(sContent.GetData(), 1, sContent.GetByteLength(), stdout);
	}

	void ConsoleOutputDevice::Flush()
	{
		fflush(stdout);
	}

	// =========================================================================
	// FileOutputDevice
	// =========================================================================

	bool FileOutputDevice::Open(const VspString& sFilePath)
	{
		Close();

		m_FilePath = sFilePath;

#if defined(_MSC_VER)
		// fopen_s is the MSVC/Annex-K spelling; elsewhere the standard call is the
		// only one there is, and the guard keeps this file portable.
		const errno_t eOpenResult = fopen_s(&m_pFile, sFilePath.GetData(), "ab");
		const bool bIsOpen = (eOpenResult == 0) && (m_pFile != nullptr);
#else
		m_pFile = fopen(sFilePath.GetData(), "ab");
		const bool bIsOpen = (m_pFile != nullptr);
#endif
		if (!bIsOpen)
		{
			// Every line a closed device receives is dropped silently, so the
			// failure is reported HERE, where it was detected, rather than being
			// discovered by whoever wonders why the file stayed empty.
			m_pFile = nullptr;
			LOG_ERROR(kLogTag, "The output file '{}' could not be opened for appending.", sFilePath.GetData());
			return false;
		}
		return true;
	}

	void FileOutputDevice::Close()
	{
		if (m_pFile != nullptr)
		{
			fclose(m_pFile);
			m_pFile = nullptr;
		}
	}

	void FileOutputDevice::Write(const VspString& sContent)
	{
		if (m_pFile != nullptr)
		{
			fwrite(sContent.GetData(), 1, sContent.GetByteLength(), m_pFile);
		}
	}

	void FileOutputDevice::Flush()
	{
		if (m_pFile != nullptr)
		{
			fflush(m_pFile);
		}
	}

	// =========================================================================
	// OutputDeviceRegistry
	// =========================================================================

	OutputDeviceRegistry::OutputDeviceRegistry()
	{
		RegisterDevice(&m_DebugDevice);
		RegisterDevice(&m_ConsoleDevice);
	}

	OutputDeviceRegistry& OutputDeviceRegistry::Get()
	{
		// The registry owns this service: it is created here on first use,
		// reports a lookup from any thread but the one that created it, and is
		// destroyed explicitly by EngineServices::ShutdownAll().
		return EngineServices::GetService<OutputDeviceRegistry>("OutputDeviceRegistry");
	}

	void OutputDeviceRegistry::RegisterDevice(OutputDevice* pDevice)
	{
		if (pDevice == nullptr)
		{
			return;
		}

		// Avoid double registration of the same device pointer.
		for (size_t nIndex = 0; nIndex < m_Devices.GetSize(); ++nIndex)
		{
			if (m_Devices[nIndex] == pDevice)
			{
				return;
			}
		}

		m_Devices.Add(pDevice);
	}

	uint32 OutputDeviceRegistry::GetDeviceCount() const
	{
		return static_cast<uint32>(m_Devices.GetSize());
	}

	OutputDevice* OutputDeviceRegistry::GetDeviceAt(uint32 uIndex) const
	{
		if (uIndex >= m_Devices.GetSize())
		{
			// DEBUG_BREAK compiles out of a release build, so the range is reported
			// through the log as well: a caller that asked for a device which does
			// not exist has to be able to see that.
			LOG_ERROR(kLogTag, "GetDeviceAt: index {} is out of the {}-device range.",
				uIndex, static_cast<uint32>(m_Devices.GetSize()));
			DEBUG_BREAK();
			return nullptr;
		}
		return m_Devices[uIndex];
	}

	OutputDevice* OutputDeviceRegistry::FindDeviceByName(const VspString& sDeviceName) const
	{
		for (size_t nIndex = 0; nIndex < m_Devices.GetSize(); ++nIndex)
		{
			if (sDeviceName.Equals(m_Devices[nIndex]->GetDeviceName()))
			{
				return m_Devices[nIndex];
			}
		}
		return nullptr;
	}
}
