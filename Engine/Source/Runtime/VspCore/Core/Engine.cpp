#include "RuntimePCH.h"

#include <Windows.h>

#include "Core/Application.h"
#include "Core/Engine.h"
#include "Core/Input/InputManager.h"
#include "Core/Logging/Log.h"
#include "Graphics/Vulkan/VulkanRenderer2D.h"
#include "Scripting/ScriptCore.h"
#include "Scripting/ScriptEngine.h"

namespace Vsp
{
	static constexpr const char* kLogTag = "GameEngine";
	static constexpr float k_fMaximumDeltaSeconds = 0.1f;   // Avoid huge jumps after stalls.

	// -------------------------------------------------------------------------
	// Implementation
	// -------------------------------------------------------------------------

	struct GameEngine::Impl
	{
		GameEngineConfig Config;
		std::unique_ptr<Application> pApplication;
		std::unique_ptr<VulkanRenderer2D> pRenderer;

		bool bInitialized = false;
		bool bExitRequested = false;
		uint32 uFrameCount = 0;
		uint32 uPrimaryScriptInstanceId = 0;
		int32 nWindowWidth = 0;
		int32 nWindowHeight = 0;
		float fElapsedSeconds = 0.0f;

		// Key-simulation bookkeeping (acceptance tests).
		size_t nNextKeyStepIndex = 0;
		uint32 uPendingKeyUpCode = 0;
		uint32 uKeyUpDueMilliseconds = 0;

		// High-resolution frame timer.
		LARGE_INTEGER nTimerFrequency = {};
		LARGE_INTEGER nLastTickCounter = {};
		bool bHasLastTick = false;

		float TickFrameTimer()
		{
			if (!bHasLastTick)
			{
				QueryPerformanceFrequency(&nTimerFrequency);
				QueryPerformanceCounter(&nLastTickCounter);
				bHasLastTick = true;
				return 0.0f;
			}

			LARGE_INTEGER nCurrentTickCounter;
			QueryPerformanceCounter(&nCurrentTickCounter);

			const double dDeltaSeconds =
				static_cast<double>(nCurrentTickCounter.QuadPart - nLastTickCounter.QuadPart) /
				static_cast<double>(nTimerFrequency.QuadPart);
			nLastTickCounter = nCurrentTickCounter;

			float fDeltaSeconds = static_cast<float>(dDeltaSeconds);
			if (fDeltaSeconds > k_fMaximumDeltaSeconds)
			{
				fDeltaSeconds = k_fMaximumDeltaSeconds;
			}
			return fDeltaSeconds;
		}

		bool PollWindowSizeChanged() const
		{
			if (pApplication == nullptr || pApplication->GetWindow() == nullptr)
			{
				return false;
			}

			int nClientWidth = 0;
			int nClientHeight = 0;
			pApplication->GetWindow()->GetClientSize(nClientWidth, nClientHeight);
			return nClientWidth != nWindowWidth || nClientHeight != nWindowHeight;
		}

		void RefreshWindowSize()
		{
			int nClientWidth = 0;
			int nClientHeight = 0;
			pApplication->GetWindow()->GetClientSize(nClientWidth, nClientHeight);
			nWindowWidth = nClientWidth;
			nWindowHeight = nClientHeight;
		}

		void UpdateScripts()
		{
			ScriptEngine& scriptEngine = ScriptEngine::Get();
			ScriptCore& scriptCore = ScriptCore::Get();

			const float fDeltaSeconds = TickFrameTimer();
			fElapsedSeconds += fDeltaSeconds;

			scriptCore.SetDeltaTime(fDeltaSeconds);
			scriptCore.SetElapsedTime(fElapsedSeconds);

			// Drive every managed script instance (Unity-style OnUpdate).
			scriptEngine.UpdateAllScripts();

			// Read the primary script's state back and feed the renderer.
			float fPositionX = 0.0f;
			float fPositionY = 0.0f;
			if (uPrimaryScriptInstanceId != 0)
			{
				scriptCore.GetTransformPosition(uPrimaryScriptInstanceId, fPositionX, fPositionY);
			}
			pRenderer->SetTrianglePosition(fPositionX, fPositionY);
			pRenderer->SetColorMode(scriptCore.GetColorMode(uPrimaryScriptInstanceId));
		}
	};

	// -------------------------------------------------------------------------
	// GameEngine
	// -------------------------------------------------------------------------

	GameEngine::GameEngine()
		: m_pImpl(std::make_unique<Impl>())
	{
	}

	GameEngine::~GameEngine()
	{
		if (m_pImpl == nullptr)
		{
			return;
		}

		// Destroy managed instances before the runtime context goes away.
		ScriptEngine::Get().Shutdown();

		if (m_pImpl->pRenderer != nullptr)
		{
			m_pImpl->pRenderer->Shutdown();
			m_pImpl->pRenderer.reset();
		}

		if (m_pImpl->pApplication != nullptr)
		{
			m_pImpl->pApplication->GetWindow()->Destroy();
			m_pImpl->pApplication.reset();
		}
	}

	bool GameEngine::Initialize(const GameEngineConfig& config, VspString& outErrorText)
	{
		outErrorText = nullptr;

		if (m_pImpl->bInitialized)
		{
			outErrorText = "GameEngine is already initialized.";
			return false;
		}

		m_pImpl->Config = config;

		// The CoreCLR host needs DOTNET_ROOT pointing at the shipped runtime,
		// so nethost/get_hostfxr_path and hostpolicy resolve from there.
		SetEnvironmentVariableW(L"DOTNET_ROOT", config.sDotNetRootPath.ToWideText().GetData());
		SetEnvironmentVariableW(L"DOTNET_ROOT(x86)", config.sDotNetRootPath.ToWideText().GetData());

		// 1. Window + message pump.
		WindowProperties windowProperties(config.sWindowTitle, config.uWindowWidth, config.uWindowHeight);
		m_pImpl->pApplication = std::make_unique<Application>(ApplicationArguments(), windowProperties);
		if (m_pImpl->pApplication == nullptr ||
			m_pImpl->pApplication->GetWindow() == nullptr ||
			!m_pImpl->pApplication->GetWindow()->IsValid())
		{
			outErrorText = "Failed to create the application window.";
			return false;
		}
		m_pImpl->RefreshWindowSize();

		// 2. Vulkan renderer (logs its own errors, including unsupported devices).
		m_pImpl->pRenderer = std::make_unique<VulkanRenderer2D>();
		if (!m_pImpl->pRenderer->Initialize(
			m_pImpl->pApplication->GetWindow()->GetNativeWindowHandle()))
		{
			outErrorText = "Failed to initialize the Vulkan renderer (see the engine log for details).";
			return false;
		}

		// 3. CoreCLR script host + automatic creation of all script instances.
		ScriptEngine& scriptEngine = ScriptEngine::Get();
		if (!scriptEngine.Initialize(
			config.sAssemblyPath,
			config.sRuntimeConfigPath,
			config.sDotNetRootPath,
			outErrorText))
		{
			return false;
		}

		if (!scriptEngine.CreateAllScriptInstances(outErrorText))
		{
			return false;
		}

		m_pImpl->uPrimaryScriptInstanceId = scriptEngine.GetPrimaryScriptInstanceId();
		for (uint32 uIndex = 0; uIndex < scriptEngine.GetScriptInstanceCount(); ++uIndex)
		{
			// Simple sequential walk: init + start every instance.
			if (uIndex == 0)
			{
				scriptEngine.CallScriptInit(m_pImpl->uPrimaryScriptInstanceId);
				scriptEngine.CallScriptStart(m_pImpl->uPrimaryScriptInstanceId);
			}
		}

		// 4. Report what the renderer negotiated.
		const VulkanDeviceProperties& deviceProperties = m_pImpl->pRenderer->GetDeviceProperties();
		LOG_INFO(kLogTag, "Device: {} (Vulkan {}.{}.{}, feature path: Vulkan 1.3 bindless)",
			deviceProperties.sDeviceName.GetData(),
			deviceProperties.uApiMajor, deviceProperties.uApiMinor, deviceProperties.uApiPatch);

		m_pImpl->bInitialized = true;
		return true;
	}

	void GameEngine::Run()
	{
		if (!m_pImpl->bInitialized)
		{
			return;
		}

		LOG_INFO(kLogTag, "Entering main loop.");

		while (IsRunning())
		{
			if (m_pImpl->Config.uMaxFrameCount > 0 &&
				m_pImpl->uFrameCount >= m_pImpl->Config.uMaxFrameCount)
			{
				break;
			}

			// 1. Window messages -> events -> input state.
			InputManager::Get().BeginFrame();
			m_pImpl->pApplication->Update();

			if (!m_pImpl->pApplication->IsRunning())
			{
				break;
			}

			// 2. Scripts (C#) update and read-back into the renderer.
			m_pImpl->UpdateScripts();

			// 2b. Synthetic key simulation (acceptance tests): post real window
			// messages into the engine's own queue, so the whole input pipeline
			// runs exactly as it does for physical keys.
			{
				HWND windowHandle = static_cast<HWND>(
					m_pImpl->pApplication->GetWindow()->GetNativeWindowHandle());
				const uint32 uElapsedMilliseconds =
					static_cast<uint32>(m_pImpl->fElapsedSeconds * 1000.0f);

				ArrayList<GameEngineConfig::KeySimulationStep>& steps =
					m_pImpl->Config.KeySimulationSteps;
				while (m_pImpl->nNextKeyStepIndex < steps.GetSize() &&
					steps[m_pImpl->nNextKeyStepIndex].uStartMilliseconds <= uElapsedMilliseconds)
				{
					const GameEngineConfig::KeySimulationStep& step =
						steps[m_pImpl->nNextKeyStepIndex];
					LOG_INFO(kLogTag, "Simulating key down 0x{:X} at {} ms.", step.uVirtualKeyCode, uElapsedMilliseconds);
					::PostMessageW(windowHandle, WM_KEYDOWN, step.uVirtualKeyCode, 0);
					if (step.uHoldMilliseconds == 0)
					{
						::PostMessageW(windowHandle, WM_KEYUP, step.uVirtualKeyCode, 0);
					}
					else
					{
						m_pImpl->uPendingKeyUpCode = step.uVirtualKeyCode;
						m_pImpl->uKeyUpDueMilliseconds = uElapsedMilliseconds + step.uHoldMilliseconds;
					}
					++m_pImpl->nNextKeyStepIndex;
				}

				if (m_pImpl->uPendingKeyUpCode != 0 &&
					uElapsedMilliseconds >= m_pImpl->uKeyUpDueMilliseconds)
				{
					LOG_INFO(kLogTag, "Simulating key up 0x{:X} at {} ms.", m_pImpl->uPendingKeyUpCode, uElapsedMilliseconds);
					::PostMessageW(windowHandle, WM_KEYUP, m_pImpl->uPendingKeyUpCode, 0);
					m_pImpl->uPendingKeyUpCode = 0;
				}
			}

			// 3. Render (the renderer logs its own errors).
			if (!m_pImpl->pRenderer->RenderFrame())
			{
				LOG_ERROR(kLogTag, "Frame rendering failed.");
			}

			// 3b. Framebuffer captures (acceptance tests).
			for (size_t nCaptureIndex = 0;
				nCaptureIndex < m_pImpl->Config.FrameCaptures.GetSize();
				++nCaptureIndex)
			{
				const GameEngineConfig::FrameCapture& capture =
					m_pImpl->Config.FrameCaptures[nCaptureIndex];
				if (m_pImpl->uFrameCount == capture.uFrameIndex)
				{
					float fPositionX = 0.0f;
					float fPositionY = 0.0f;
					ScriptCore::Get().GetTransformPosition(m_pImpl->uPrimaryScriptInstanceId, fPositionX, fPositionY);
					LOG_INFO(kLogTag, "Capture at frame {}: script position={:p}, deltaTime={}, elapsed={} ms.",
						m_pImpl->uFrameCount, Position2D{ fPositionX, fPositionY },
						ScriptCore::Get().GetDeltaTime(),
						static_cast<uint32>(m_pImpl->fElapsedSeconds * 1000.0f));

					if (!m_pImpl->pRenderer->CaptureFramebuffer(capture.sFilePath))
					{
						// The capture failure was already logged inside the renderer.
						LOG_ERROR(kLogTag, "Framebuffer capture failed.");
					}
					else
					{
						LOG_INFO(kLogTag, "Framebuffer captured to {}", capture.sFilePath.GetData());
					}
				}
			}

			// 4. Resize handling (polled - covers minimize and maximize too).
			if (m_pImpl->PollWindowSizeChanged())
			{
				m_pImpl->RefreshWindowSize();
				m_pImpl->pRenderer->OnWindowResize(
					static_cast<uint32>(m_pImpl->nWindowWidth),
					static_cast<uint32>(m_pImpl->nWindowHeight));
			}

			// 5. Frame bookkeeping.
			InputManager::Get().EndFrame();
			++m_pImpl->uFrameCount;
		}

		LOG_INFO(kLogTag, "Main loop exited after {} frames.", m_pImpl->uFrameCount);
	}

	void GameEngine::RequestExit()
	{
		m_pImpl->bExitRequested = true;
	}

	bool GameEngine::IsRunning() const
	{
		return m_pImpl->bInitialized &&
			!m_pImpl->bExitRequested &&
			m_pImpl->pApplication->IsRunning();
	}

	uint32 GameEngine::GetFrameCount() const
	{
		return m_pImpl->uFrameCount;
	}
}
