#pragma once

#include <cstdio>

#include "Core/Core.h"
#include "Core/String/VspString.h"
#include "Core/Templates/ArrayList.h"

namespace Vsp
{
	// -------------------------------------------------------------------------
	// OutputDevice
	// -------------------------------------------------------------------------
	// A sink the engine can write text to: the debugger, the console, a log
	// file, and so on. Devices are addressable by name through the
	// OutputDeviceRegistry so callers can enumerate ("get") the available
	// output devices and then write output content into them.
	// -------------------------------------------------------------------------
	class RUNTIME_API OutputDevice
	{
	public:
		virtual ~OutputDevice() = default;

		// Stable, human-readable device identifier ("Debug", "Console", "File").
		virtual const char* GetDeviceName() const = 0;

		// True when the device currently accepts output.
		virtual bool IsOpen() const { return true; }

		// Writes the given text to the device.
		virtual void Write(const VspString& sContent) = 0;

		// Pushes any buffered content out (no-op for unbuffered devices).
		virtual void Flush() {}
	};

	// Writes into the debugger's output window (OutputDebugStringA).
	class RUNTIME_API DebugOutputDevice : public OutputDevice
	{
	public:
		const char* GetDeviceName() const override { return "Debug"; }
		void Write(const VspString& sContent) override;
	};

	// Writes to the process standard output stream.
	class RUNTIME_API ConsoleOutputDevice : public OutputDevice
	{
	public:
		const char* GetDeviceName() const override { return "Console"; }
		void Write(const VspString& sContent) override;
		void Flush() override;
	};

	// Writes to a UTF-8 text file. Open() (re)opens the target path; a failed
	// open leaves the device closed and writes are discarded.
#pragma warning(push)
#pragma warning(disable : 4251)   // VspString member: header-only template.
	class RUNTIME_API FileOutputDevice : public OutputDevice
	{
	public:
		FileOutputDevice() = default;
		explicit FileOutputDevice(const VspString& sFilePath) { Open(sFilePath); }

		~FileOutputDevice() override { Close(); }

		bool Open(const VspString& sFilePath);
		void Close();
		bool IsOpen() const override { return m_pFile != nullptr; }
		const VspString& GetFilePath() const { return m_FilePath; }

		const char* GetDeviceName() const override { return "File"; }
		void Write(const VspString& sContent) override;
		void Flush() override;

	private:
		FILE* m_pFile = nullptr;
		VspString m_FilePath;
	};
#pragma warning(pop)

	// -------------------------------------------------------------------------
	// OutputDeviceRegistry
	// -------------------------------------------------------------------------
	// Owns the built-in devices (Debug, Console) and lets callers enumerate
	// them or look them up by name. File devices are created per target path
	// with FileOutputDevice and can be registered through RegisterDevice().
	// -------------------------------------------------------------------------
#pragma warning(push)
#pragma warning(disable : 4251)   // Non-exported device members: see FileOutputDevice above.
	class RUNTIME_API OutputDeviceRegistry
	{
	public:
		static OutputDeviceRegistry& Get();

		void RegisterDevice(OutputDevice* pDevice);
		uint32 GetDeviceCount() const;
		OutputDevice* GetDeviceAt(uint32 uIndex) const;
		OutputDevice* FindDeviceByName(const VspString& sDeviceName) const;

	private:
		OutputDeviceRegistry();

		DebugOutputDevice m_DebugDevice;
		ConsoleOutputDevice m_ConsoleDevice;
		ArrayList<OutputDevice*> m_Devices;
	};
#pragma warning(pop)
}
