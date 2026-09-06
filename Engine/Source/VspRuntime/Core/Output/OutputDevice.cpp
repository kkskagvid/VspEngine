#include "RuntimePCH.h"

#include <cstdio>

#include <Windows.h>

#include "Core/Output/OutputDevice.h"

namespace Vsp
{
	// =========================================================================
	// DebugOutputDevice
	// =========================================================================

	void DebugOutputDevice::Write(const VspString& sContent)
	{
		OutputDebugStringA(sContent.GetData());
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
		fopen_s(&m_pFile, sFilePath.GetData(), "ab");
		return m_pFile != nullptr;
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
		static OutputDeviceRegistry s_Instance;
		return s_Instance;
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
