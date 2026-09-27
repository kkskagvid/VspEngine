#pragma warning(disable : 4996)
#include <chrono>
#include <cstdlib>

#include "Common/PlatformMisc.h"
#include "Core/Core.h"
#include "Core/Engine.h"
#include "Core/EngineServices.h"
#include "Core/Logging/Log.h"
#include "Core/Logging/LogBackend.h"
#include "Core/Output/OutputDevice.h"
#include "Core/String/VspString.h"

#include "LaunchPlatform.h"

namespace Vsp
{
	static constexpr const char* kLogTag = "Launch";

	// -------------------------------------------------------------------------
	// Command line parsing
	// -------------------------------------------------------------------------

	struct LaunchOptions
	{
		VspString sWindowTitle = "Vsp Engine";
		uint32 uWindowWidth = 1280;
		uint32 uWindowHeight = 720;
		uint32 uMaxFrameCount = 0;             // 0 = unlimited
		float fFixedDeltaMilliseconds = 0.0f;    // 0 = wall clock; > 0 = fixed frame step (deterministic runs)
		bool bShowErrorDialog = true;            // false = write errors to the log only (automation)
		bool bAllowCursorLock = true;            // false = never take the user's pointer (automation)
		VspString sEngineAssemblyPath;           // defaults to <exe dir>\VspEngine.dll
		VspString sAssemblyPath;                 // defaults to <exe dir>\Assembly.dll (game Assembly)
		VspString sRuntimeConfigPath;            // defaults to <exe dir>\Launch.runtimeconfig.json
		VspString sDotNetRootPath;               // defaults to <exe dir>\Binaries\dotnet\runtime\10.0.10
		ArrayList<GameEngineConfig::KeySimulationStep> KeySimulationSteps;
		ArrayList<GameEngineConfig::MouseSimulationStep> MouseSimulationSteps;
		uint32 uKeyScriptCursorMilliseconds = 800;   // First synthetic key fires 800 ms in.
		uint32 uMouseScriptCursorMilliseconds = 800; // First synthetic mouse move fires 800 ms in.
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

	// Parses "X:Y" or "X:Y:MS" into a synthetic mouse-move step (client
	// coordinates, and optionally the elapsed-time offset it fires at).
	static bool ParseMouseSimulationValue(const wchar_t* pValue, GameEngineConfig::MouseSimulationStep& outStep)
	{
		if (pValue == nullptr)
		{
			return false;
		}

		VspString sValue(pValue);
		const size_t nXColonIndex = sValue.Find(":");
		if (nXColonIndex == VspString::InvalidIndex)
		{
			return false;
		}

		const size_t nYColonIndex = sValue.Find(":", nXColonIndex + 1);
		const size_t nYEndIndex = (nYColonIndex == VspString::InvalidIndex) ? sValue.GetByteLength() : nYColonIndex;

		VspString sXPart = sValue.GetSubString(0, nXColonIndex);
		VspString sYPart = sValue.GetSubString(nXColonIndex + 1, nYEndIndex - nXColonIndex - 1);

		outStep.nClientX = static_cast<int32>(wcstol(sXPart.ToWideText().GetData(), nullptr, 10));
		outStep.nClientY = static_cast<int32>(wcstol(sYPart.ToWideText().GetData(), nullptr, 10));

		// The optional third field pins the step to an elapsed-time offset
		// instead of the running cursor, which is how a test places a mouse move
		// between two key presses.
		if (nYColonIndex != VspString::InvalidIndex)
		{
			VspString sTimePart = sValue.GetSubString(nYColonIndex + 1, sValue.GetByteLength() - nYColonIndex - 1);
			outStep.uStartMilliseconds = static_cast<uint32>(wcstoul(sTimePart.ToWideText().GetData(), nullptr, 10));
			outStep.bHasExplicitStartTime = true;
		}
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

	// True for every option this host knows, in both its "--name=value" and its
	// "--name value" spelling, so an unknown one can be reported rather than
	// ignored. The check is on the NAME, so "--width=800" and "--width" are both
	// known here even though only one of them carries its value.
	static bool IsKnownCommandLineOption(const VspString& sArgument)
	{
		static constexpr const char* k_pKnownOptionNames[] =
		{
			"--frames", "--width", "--height", "--fixed-delta-time", "--title",
			"--engine-assembly", "--assembly", "--runtime-config", "--dotnet-root",
			"--key", "--mouse", "--capture", "--silent", "--no-cursor-lock",
		};

		// The name ends where an "=" starts: "--title=Foo" is the option "--title".
		VspString sOptionName = sArgument;
		const size_t nEqualsIndex = sArgument.Find("=");
		if (nEqualsIndex != VspString::InvalidIndex)
		{
			sOptionName = sArgument.GetSubString(0, nEqualsIndex);
		}

		for (const char* pKnownName : k_pKnownOptionNames)
		{
			if (sOptionName.Equals(pKnownName))
			{
				return true;
			}
		}
		return false;
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
		else if ((pValue = ReadValueAfter(pArgument, L"--fixed-delta-time=")) != nullptr)
		{
			options.fFixedDeltaMilliseconds = static_cast<float>(wcstod(pValue, nullptr));
		}
		else if ((pValue = ReadValueAfter(pArgument, L"--title=")) != nullptr)
		{
			options.sWindowTitle = VspString(pValue);
		}
		else if ((pValue = ReadValueAfter(pArgument, L"--engine-assembly=")) != nullptr)
		{
			options.sEngineAssemblyPath = VspString(pValue);
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
		else if (sArgument.Equals("--silent"))
		{
			options.bShowErrorDialog = false;
		}
		else if (sArgument.Equals("--no-cursor-lock"))
		{
			// An automated run happens on someone's desktop: it must not hide their
			// pointer or move it to the middle of a window.
			options.bAllowCursorLock = false;
		}
		else if (IsKnownCommandLineOption(sArgument))
		{
			// A known option whose value is consumed by the caller: nothing to do
			// here, and not a mistake.
		}
		else
		{
			// A typo used to be accepted in silence, which made "--widht=800" look
			// like it worked. Every argument the host does not know is reported.
			LOG_WARNING(kLogTag, "The command line option '{}' is not known and is ignored.", sArgument.GetData());
		}
	}

	// Locates the shipped .NET runtime relative to the executable. The runtime
	// is staged either next to the executable (a self-contained run directory)
	// or stays in the repository's Binaries tree, so the same executable works
	// from both layouts: the first candidate that actually holds nethost.dll
	// wins. --dotnet-root overrides the whole probe.
	static VspString ResolveDefaultDotNetRootPath(const VspString& sExecutableDirectory)
	{
		static constexpr uint32 k_nCandidateCount = 3;
		const VspString sCandidates[k_nCandidateCount] =
		{
			sExecutableDirectory + "\\Binaries\\dotnet\\runtime\\10.0.10",
            sExecutableDirectory + "\\..\\..\\..\\Binaries\\dotnet\\runtime\\10.0.10", // Test runs from the repository's Binaries tree.
		};

		for (uint32 uCandidateIndex = 0; uCandidateIndex < k_nCandidateCount; ++uCandidateIndex)
		{
			if (PlatformMisc::DoesFileExist(sCandidates[uCandidateIndex] + "\\host\\nethost.dll"))
			{
				return sCandidates[uCandidateIndex];
			}
		}

		// Nothing matched: fall back to the staged layout so the error message
		// names the place the stager would have used.
		return sCandidates[0];
	}

	// -------------------------------------------------------------------------
	// Main loop
	// -------------------------------------------------------------------------

	int32 RunLaunchLoop(int32 nArgumentCount, wchar_t** pArguments)
	{
		LaunchOptions options;

		// Default paths are derived from the executable location (the
		// platform-specific lookup is encapsulated in Common/PlatformMisc).
		const VspString sExecutableDirectory = PlatformMisc::GetExecutableDirectoryPath();
		options.sEngineAssemblyPath = sExecutableDirectory + "\\VspEngine.dll";
		options.sAssemblyPath = sExecutableDirectory + "\\Assembly.dll";
		options.sRuntimeConfigPath = sExecutableDirectory + "\\Launch.runtimeconfig.json";
		options.sDotNetRootPath = ResolveDefaultDotNetRootPath(sExecutableDirectory);

		for (int32 nIndex = 1; nIndex < nArgumentCount; ++nIndex)
		{
			ApplyCommandLineOption(options, pArguments[nIndex]);

			// Consume the value of space-separated options.
			VspString sArgument(pArguments[nIndex]);
			const bool bNeedsValue = sArgument.Equals("--frames") ||
				sArgument.Equals("--width") || sArgument.Equals("--height") ||
				sArgument.Equals("--fixed-delta-time") ||
				sArgument.Equals("--title") || sArgument.Equals("--engine-assembly") ||
				sArgument.Equals("--assembly") ||
				sArgument.Equals("--runtime-config") || sArgument.Equals("--dotnet-root") ||
				sArgument.Equals("--key") || sArgument.Equals("--mouse") || sArgument.Equals("--capture");
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
				else if (sArgument.Equals("--fixed-delta-time"))
				{
					options.fFixedDeltaMilliseconds = static_cast<float>(wcstod(pValue, nullptr));
				}
				else if (sArgument.Equals("--title"))
				{
					options.sWindowTitle = VspString(pValue);
				}
				else if (sArgument.Equals("--engine-assembly"))
				{
					options.sEngineAssemblyPath = VspString(pValue);
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
					else
					{
						// A malformed schedule used to be dropped in silence, which
						// made a typo look like a key that never fired.
						LOG_ERROR(kLogTag, "--key expects VKCODE:HOLD_MS, got '{}'; the step is ignored.",
							VspString(pValue).GetData());
					}
				}
				else if (sArgument.Equals("--mouse"))
				{
					GameEngineConfig::MouseSimulationStep mouseStep;
					if (ParseMouseSimulationValue(pValue, mouseStep))
					{
						if (!mouseStep.bHasExplicitStartTime)
						{
							mouseStep.uStartMilliseconds = options.uMouseScriptCursorMilliseconds;
							options.uMouseScriptCursorMilliseconds += 100;
						}
						options.MouseSimulationSteps.Add(mouseStep);
					}
					else
					{
						LOG_ERROR(kLogTag, "--mouse expects X:Y[:START_MS], got '{}'; the step is ignored.",
							VspString(pValue).GetData());
					}
				}
				else if (sArgument.Equals("--capture"))
				{
					GameEngineConfig::FrameCapture capture;
					if (ParseCaptureValue(pValue, capture))
					{
						options.FrameCaptures.Add(capture);
					}
					else
					{
						LOG_ERROR(kLogTag, "--capture expects FRAME:PATH, got '{}'; the capture is ignored.",
							VspString(pValue).GetData());
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

		// The clock is READ and CHECKED: localtime returns null when the value it
		// is given cannot be represented, and dereferencing that was undefined
		// behaviour on a machine whose clock is out of range. A run that cannot
		// name its own start time still runs - the log file is simply named after
		// the fallback below.
		VspString sTimeStampText("unknown-time");
		const time_t nNowTime = time(nullptr);
		if (nNowTime != static_cast<time_t>(-1))
		{
			const tm* pLocalTime = localtime(&nNowTime);
			if (pLocalTime != nullptr)
			{
				sTimeStampText = VspFormat::Format("{:04}-{:02}-{:02}-{:02}-{:02}-{:02}",
					pLocalTime->tm_year + 1900, pLocalTime->tm_mon + 1, pLocalTime->tm_mday,
					pLocalTime->tm_hour, pLocalTime->tm_min, pLocalTime->tm_sec);
			}
			else
			{
				LOG_WARNING(kLogTag, "The system clock could not be converted to local time; the log file uses a fallback name.");
			}
		}
		else
		{
			LOG_WARNING(kLogTag, "The system clock could not be read; the log file uses a fallback name.");
		}

		const VspString sLogFilePath = sExecutableDirectory + "\\" + sTimeStampText + ".log";
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

		LOG_INFO(kLogTag, "Launch starting (engine assembly: {}, game assembly: {}, dotnet root: {}).",
			options.sEngineAssemblyPath.GetData(), options.sAssemblyPath.GetData(), options.sDotNetRootPath.GetData());

		GameEngineConfig config;
		config.sWindowTitle = options.sWindowTitle;
		config.uWindowWidth = options.uWindowWidth;
		config.uWindowHeight = options.uWindowHeight;
		config.uMaxFrameCount = options.uMaxFrameCount;
		config.fFixedDeltaSeconds = options.fFixedDeltaMilliseconds > 0.0f
			? options.fFixedDeltaMilliseconds / 1000.0f
			: 0.0f;
		config.bAllowCursorLock = options.bAllowCursorLock;
		config.sEngineAssemblyPath = options.sEngineAssemblyPath;
		config.sAssemblyPath = options.sAssemblyPath;
		config.sRuntimeConfigPath = options.sRuntimeConfigPath;
		config.sDotNetRootPath = options.sDotNetRootPath;
		config.KeySimulationSteps = options.KeySimulationSteps;
		config.MouseSimulationSteps = options.MouseSimulationSteps;
		config.FrameCaptures = options.FrameCaptures;

		// The HOST owns the process environment, not the engine: this is the
		// platform entry (Launch/Windows/LaunchPlatformWindows.cpp) putting
		// DOTNET_ROOT in place before the CoreCLR bootstrap runs, so the engine
		// core never has to know how a platform resolves its runtime.
		ApplyLaunchPlatformEnvironment(options.sDotNetRootPath);

		{
			// The engine lives in its own scope: its destructor is what shuts the
			// script host, the renderer and the scene down, and it has to have run
			// before the services those point at are destroyed below.
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
		}

		Log::Flush();

		// Teardown order is explicit, and it is the host that decides it:
		//   1. the engine's services are destroyed, in reverse creation order, by
		//      the registry - instead of at process exit in an order no one can
		//      see. This is the last thing the engine logs, and it is logged while
		//      the output devices still exist;
		//   2. the log backends stop being registered, because they point at
		//      output devices one of those services owned (Log does not own them).
		EngineServices::ShutdownAll();
		Log::RemoveBackend(&s_FileLogBackend);
		Log::RemoveBackend(&s_ConsoleLogBackend);
		Log::RemoveBackend(&s_DebugLogBackend);

		return 0;
	}
}
