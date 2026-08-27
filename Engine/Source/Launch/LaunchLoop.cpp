#include <Windows.h>

#include <cstdlib>

#include "Core/Core.h"
#include "Core/Engine.h"
#include "Core/Logging/Log.h"
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
		uint32_t uWindowWidth = 1280;
		uint32_t uWindowHeight = 720;
		uint32_t uMaxFrameCount = 0;             // 0 = unlimited
		bool bShowErrorDialog = true;            // false = write errors to the log only (automation)
		VspString sAssemblyPath;                 // defaults to <exe dir>\VspPlayer.dll
		VspString sRuntimeConfigPath;            // defaults to <exe dir>\Launch.runtimeconfig.json
		VspString sDotNetRootPath;               // defaults to <exe dir>\..\..\..\Binaries\dotnet\runtime10.0.10
		ArrayList<GameEngineConfig::KeySimulationStep> KeySimulationSteps;
		uint32_t uKeyScriptCursorMilliseconds = 800;   // First synthetic key fires 800 ms in.
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

		outStep.uVirtualKeyCode = static_cast<uint32_t>(wcstoul(sKeyPart.ToWideText().GetData(), nullptr, 0));
		outStep.uHoldMilliseconds = static_cast<uint32_t>(wcstoul(sDurationPart.ToWideText().GetData(), nullptr, 10));
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
		outCapture.uFrameIndex = static_cast<uint32_t>(wcstoul(sFramePart.ToWideText().GetData(), nullptr, 10));
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
			options.uMaxFrameCount = static_cast<uint32_t>(wcstoul(pValue, nullptr, 10));
		}
		else if ((pValue = ReadValueAfter(pArgument, L"--width=")) != nullptr)
		{
			options.uWindowWidth = static_cast<uint32_t>(wcstoul(pValue, nullptr, 10));
		}
		else if ((pValue = ReadValueAfter(pArgument, L"--height=")) != nullptr)
		{
			options.uWindowHeight = static_cast<uint32_t>(wcstoul(pValue, nullptr, 10));
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

	int32_t RunLaunchLoop(int32_t nArgumentCount, wchar_t** pArguments)
	{
		LaunchOptions options;

		// Default paths are derived from the executable location.
		const VspString sExecutableDirectory = GetExecutableDirectoryPath();
		options.sAssemblyPath = sExecutableDirectory + "\\VspPlayer.dll";
		options.sRuntimeConfigPath = sExecutableDirectory + "\\Launch.runtimeconfig.json";
		options.sDotNetRootPath = sExecutableDirectory + "\\..\\..\\..\\Binaries\\dotnet\\runtime10.0.10";

		for (int32_t nIndex = 1; nIndex < nArgumentCount; ++nIndex)
		{
			ApplyCommandLineOption(options, pArguments[nIndex]);

			// Consume the value of space-separated options.
			VspString sArgument(pArguments[nIndex]);
			const bool bNeedsValue = sArgument.Equals("--frames") ||
				sArgument.Equals("--width") || sArgument.Equals("--height") ||
				sArgument.Equals("--key") || sArgument.Equals("--capture");
			if (bNeedsValue && nIndex + 1 < nArgumentCount)
			{
				const wchar_t* pValue = pArguments[nIndex + 1];
				if (sArgument.Equals("--frames"))
				{
					options.uMaxFrameCount = static_cast<uint32_t>(wcstoul(pValue, nullptr, 10));
				}
				else if (sArgument.Equals("--width"))
				{
					options.uWindowWidth = static_cast<uint32_t>(wcstoul(pValue, nullptr, 10));
				}
				else if (sArgument.Equals("--height"))
				{
					options.uWindowHeight = static_cast<uint32_t>(wcstoul(pValue, nullptr, 10));
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

		// Direct log output into a file next to the executable.
		const VspString sLogFilePath = sExecutableDirectory + "\\Launch.log";
		LogDetail::SetLogFilePath(sLogFilePath.ToStdString().c_str());
		LOG_INFO(kLogTag, "Launch starting (assembly: {}, dotnet root: {}).",
			options.sAssemblyPath.ToStdString(), options.sDotNetRootPath.ToStdString());

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
			LOG_ERROR(kLogTag, "{}", sErrorText.ToStdString());

			if (options.bShowErrorDialog)
			{
				MessageBoxW(
					nullptr,
					sErrorText.ToWideText().GetData(),
					L"Vsp Engine - Fatal Error",
					MB_OK | MB_ICONERROR);
			}
			return 1;
		}

		engine.Run();
		LOG_INFO(kLogTag, "Launch exiting cleanly after {} frames.", engine.GetFrameCount());
		return 0;
	}
}
