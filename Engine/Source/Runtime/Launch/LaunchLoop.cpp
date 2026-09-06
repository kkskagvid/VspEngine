#include <Windows.h>

#pragma warning(disable : 4996)
#include <chrono>
#include <cstdlib>

#include "Core/Core.h"
#include "Core/Engine.h"
#include "Core/Logging/Log.h"
#include "Core/Logging/LogBackend.h"
#include "Core/Output/OutputDevice.h"
#include "Core/String/VspString.h"

namespace Vsp
{
	static constexpr const char* kLogTag = "Launch";

	// -------------------------------------------------------------------------
	// Command line parsing
	// -------------------------------------------------------------------------

	struct LaunchOptions
	{
		VspString sWindowTitle = "Vsp Engine - Triangle Demo";
		uint32 uWindowWidth = 1280;
		uint32 uWindowHeight = 720;
		uint32 uMaxFrameCount = 0;             // 0 = unlimited
		bool bShowErrorDialog = true;            // false = write errors to the log only (automation)
		VspString sAssemblyPath;                 // defaults to <exe dir>\VspPlayer.dll
		VspString sRuntimeConfigPath;            // defaults to <exe dir>\Launch.runtimeconfig.json
		VspString sDotNetRootPath;               // defaults to <exe dir>\..\..\..\Binaries\dotnet\runtime10.0.10
		ArrayList<GameEngineConfig::KeySimulationStep> KeySimulationSteps;
		uint32 uKeyScriptCursorMilliseconds = 800;   // First synthetic key fires 800 ms in.
		ArrayList<GameEngineConfig::FrameCapture> FrameCaptures;
	};

	// Parses "VK:DURATION" into a key-simulation step (VK decimal or 0x hex).
	static bool ParseKeySimulationValue(const wchar_t* pValue, GameEngineConfig::KeySimulationStep& outStep)
	{
		if (pValue == nullptr)
		{
			return false;
		}

		VspString sValue(pValue);
		const size_t nColonIndex = sValue.Find(":");
		if (nColonIndex == VspString::InvalidIndex)
		{
			return false;
		}

		VspString sKeyPart = sValue.GetSubString(0, nColonIndex);
		VspString sDurationPart = sValue.GetSubString(nColonIndex + 1, sValue.GetByteLength() - nColonIndex - 1);

		outStep.uVirtualKeyCode = static_cast<uint32>(wcstoul(sKeyPart.ToWideText().GetData(), nullptr, 0));
		outStep.uHoldMilliseconds = static_cast<uint32>(wcstoul(sDurationPart.ToWideText().GetData(), nullptr, 10));
		return true;
	}

	// Parses "FRAME:PATH" into a capture request.
	static bool ParseCaptureValue(const wchar_t* pValue, GameEngineConfig::FrameCapture& outCapture)
	{
		if (pValue == nullptr)
		{
			return false;
		}

		VspString sValue(pValue);
		const size_t nColonIndex = sValue.Find(":");
		if (nColonIndex == VspString::InvalidIndex)
		{
			return false;
		}

		VspString sFramePart = sValue.GetSubString(0, nColonIndex);
		outCapture.uFrameIndex = static_cast<uint32>(wcstoul(sFramePart.ToWideText().GetData(), nullptr, 10));
		outCapture.sFilePath = sValue.GetSubString(nColonIndex + 1, sValue.GetByteLength() - nColonIndex - 1);
		return true;
	}

	// Returns the directory containing Launch.exe, without a trailing slash.
	static VspString GetExecutableDirectoryPath()
	{
		wchar_t sExecutablePathBuffer[2048] = {};
		const DWORD nLength = GetModuleFileNameW(nullptr, sExecutablePathBuffer, 2048);
		if (nLength == 0)
		{
			return VspString();
		}

		VspString sExecutablePath(sExecutablePathBuffer);

		// Search backwards for the final path separator.
		VspString sDirectoryPath;
		for (size_t nIndex = sExecutablePath.GetByteLength(); nIndex > 0; --nIndex)
		{
			const char cCharacter = sExecutablePath.GetData()[nIndex - 1];
			if (cCharacter == '\\' || cCharacter == '/')
			{
				sDirectoryPath = sExecutablePath.GetSubString(0, nIndex - 1);
				break;
			}
		}
		return sDirectoryPath;
	}

	// Handles both "--frames=N" and "--frames N" forms.
	static const wchar_t* ReadValueAfter(const wchar_t* pArgument, const wchar_t* pOptionName)
	{
		const wchar_t* pMatch = wcsstr(pArgument, pOptionName);
		if (pMatch != pArgument)
		{
			return nullptr;
		}
		return pArgument + wcslen(pOptionName);
	}

	static void ApplyCommandLineOption(LaunchOptions& options, const wchar_t* pArgument)
	{
		if (pArgument == nullptr)
		{
			return;
		}

		VspString sArgument(pArgument);
		const wchar_t* pValue = nullptr;

		if ((pValue = ReadValueAfter(pArgument, L"--frames=")) != nullptr)
		{
			options.uMaxFrameCount = static_cast<uint32>(wcstoul(pValue, nullptr, 10));
		}
		else if ((pValue = ReadValueAfter(pArgument, L"--width=")) != nullptr)
		{
			options.uWindowWidth = static_cast<uint32>(wcstoul(pValue, nullptr, 10));
		}
		else if ((pValue = ReadValueAfter(pArgument, L"--height=")) != nullptr)
		{
			options.uWindowHeight = static_cast<uint32>(wcstoul(pValue, nullptr, 10));
		}
		else if ((pValue = ReadValueAfter(pArgument, L"--title=")) != nullptr)
		{
			options.sWindowTitle = VspString(pValue);
		}
		else if ((pValue = ReadValueAfter(pArgument, L"--assembly=")) != nullptr)
		{
			options.sAssemblyPath = VspString(pValue);
		}
		else if ((pValue = ReadValueAfter(pArgument, L"--runtime-config=")) != nullptr)
		{
			options.sRuntimeConfigPath = VspString(pValue);
		}
		else if ((pValue = ReadValueAfter(pArgument, L"--dotnet-root=")) != nullptr)
		{
			options.sDotNetRootPath = VspString(pValue);
		}
		else if (sArgument.Equals("--frames") || sArgument.Equals("--width") || sArgument.Equals("--height"))
		{
			// Space-separated numeric options are consumed together with the
			// next argument in RunLaunchLoop; marker values are ignored here.
		}
		else if (sArgument.Equals("--silent"))
		{
			options.bShowErrorDialog = false;
		}
	}

	// -------------------------------------------------------------------------
	// Main loop
	// -------------------------------------------------------------------------

	int32 RunLaunchLoop(int32 nArgumentCount, wchar_t** pArguments)
	{
		LaunchOptions options;

		// Default paths are derived from the executable location.
		const VspString sExecutableDirectory = GetExecutableDirectoryPath();
		options.sAssemblyPath = sExecutableDirectory + "\\VspPlayer.dll";
		options.sRuntimeConfigPath = sExecutableDirectory + "\\Launch.runtimeconfig.json";
		options.sDotNetRootPath = sExecutableDirectory + "\\..\\..\\..\\Binaries\\dotnet\\runtime10.0.10";

		for (int32 nIndex = 1; nIndex < nArgumentCount; ++nIndex)
		{
			ApplyCommandLineOption(options, pArguments[nIndex]);

			// Consume the value of space-separated options.
			VspString sArgument(pArguments[nIndex]);
			const bool bNeedsValue = sArgument.Equals("--frames") ||
				sArgument.Equals("--width") || sArgument.Equals("--height") ||
				sArgument.Equals("--title") || sArgument.Equals("--assembly") ||
				sArgument.Equals("--runtime-config") || sArgument.Equals("--dotnet-root") ||
				sArgument.Equals("--key") || sArgument.Equals("--capture");
			if (bNeedsValue && nIndex + 1 < nArgumentCount)
			{
				const wchar_t* pValue = pArguments[nIndex + 1];
				if (sArgument.Equals("--frames"))
				{
					options.uMaxFrameCount = static_cast<uint32>(wcstoul(pValue, nullptr, 10));
				}
				else if (sArgument.Equals("--width"))
				{
					options.uWindowWidth = static_cast<uint32>(wcstoul(pValue, nullptr, 10));
				}
				else if (sArgument.Equals("--height"))
				{
					options.uWindowHeight = static_cast<uint32>(wcstoul(pValue, nullptr, 10));
				}
				else if (sArgument.Equals("--title"))
				{
					options.sWindowTitle = VspString(pValue);
				}
				else if (sArgument.Equals("--assembly"))
				{
					options.sAssemblyPath = VspString(pValue);
				}
				else if (sArgument.Equals("--runtime-config"))
				{
					options.sRuntimeConfigPath = VspString(pValue);
				}
				else if (sArgument.Equals("--dotnet-root"))
				{
					options.sDotNetRootPath = VspString(pValue);
				}
				else if (sArgument.Equals("--key"))
				{
					GameEngineConfig::KeySimulationStep step;
					if (ParseKeySimulationValue(pValue, step))
					{
						step.uStartMilliseconds = options.uKeyScriptCursorMilliseconds;
						options.uKeyScriptCursorMilliseconds += step.uHoldMilliseconds + 300;
						options.KeySimulationSteps.Add(step);
					}
				}
				else if (sArgument.Equals("--capture"))
				{
					GameEngineConfig::FrameCapture capture;
					if (ParseCaptureValue(pValue, capture))
					{
						options.FrameCaptures.Add(capture);
					}
				}
				++nIndex;
			}
		}

		// Wire up the replaceable logging backends: debugger, console and a
		// log file next to the executable. Backends are plain borrowed
		// pointers registered with the Log facade, so the output targets can
		// be swapped at any time.
		OutputDeviceRegistry& deviceRegistry = OutputDeviceRegistry::Get();
		static FileOutputDevice s_FileOutputDevice;
		static OutputDeviceLogBackend s_DebugLogBackend(deviceRegistry.FindDeviceByName("Debug"));
		static OutputDeviceLogBackend s_ConsoleLogBackend(deviceRegistry.FindDeviceByName("Console"));
		static OutputDeviceLogBackend s_FileLogBackend(&s_FileOutputDevice);

		Log::AddBackend(&s_DebugLogBackend);
		Log::AddBackend(&s_ConsoleLogBackend);

		time_t nowtime;
		time(&nowtime);
		tm* p = localtime(&nowtime);
		VspString timeS = VspFormat::Format("{:04}-{:02}-{:02}-{:02}-{:02}-{:02}", p->tm_year + 1900, p->tm_mon + 1, p->tm_mday, p->tm_hour, p->tm_min, p->tm_sec);

		const VspString sLogFilePath = sExecutableDirectory + "\\" + timeS + ".log";
		if (s_FileOutputDevice.Open(sLogFilePath))
		{
			Log::AddBackend(&s_FileLogBackend);
		}
		else
		{
			LOG_WARNING(kLogTag, "Failed to open the log file {}; file logging is disabled.", sLogFilePath.GetData());
		}

		// --silent also disables the fatal crash prompt (automation mode).
		Log::SetCrashPromptEnabled(options.bShowErrorDialog);

		// Enumerate ("get") the available output devices and report them.
		for (uint32 uIndex = 0; uIndex < deviceRegistry.GetDeviceCount(); ++uIndex)
		{
			LOG_INFO(kLogTag, "Available output device [{}]: {}", uIndex, deviceRegistry.GetDeviceAt(uIndex)->GetDeviceName());
		}

		LOG_INFO(kLogTag, "Launch starting (assembly: {}, dotnet root: {}).",
			options.sAssemblyPath.GetData(), options.sDotNetRootPath.GetData());

		GameEngineConfig config;
		config.sWindowTitle = options.sWindowTitle;
		config.uWindowWidth = options.uWindowWidth;
		config.uWindowHeight = options.uWindowHeight;
		config.uMaxFrameCount = options.uMaxFrameCount;
		config.sAssemblyPath = options.sAssemblyPath;
		config.sRuntimeConfigPath = options.sRuntimeConfigPath;
		config.sDotNetRootPath = options.sDotNetRootPath;
		config.KeySimulationSteps = options.KeySimulationSteps;
		config.FrameCaptures = options.FrameCaptures;

		GameEngine engine;
		VspString sErrorText;
		if (!engine.Initialize(config, sErrorText))
		{
			LOG_ERROR(kLogTag, "{}", sErrorText.GetData());

			// Fatal: the Log module collects the recent log history and
			// prompts the crash dialog with it (details of the failure,
			// e.g. an unsupported Vulkan device, are part of that history).
			LOG_FATAL(kLogTag, "Engine initialization failed: {}", sErrorText.GetData());
			return 1;   // Reached only when crash prompts are disabled.
		}

		engine.Run();
		LOG_INFO(kLogTag, "Launch exiting cleanly after {} frames.", engine.GetFrameCount());
		Log::Flush();
		return 0;
	}
}
