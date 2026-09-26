#include "RuntimePCH.h"

#include "Classes/Scene.h"
#include "Classes/Time.h"
#include "Common/PlatformMisc.h"
#include "Core/Diagnostics/ErrorHandling.h"
#include "Core/Application.h"
#include "Core/Engine.h"
#include "Core/Input/InputManager.h"
#include "Core/Logging/Log.h"
#include "Graphics/GraphicsSystem.h"
#include "Graphics/RenderCore.h"
#include "Graphics/Vulkan/VulkanRenderer2D.h"
#include "Physics/PhysicsWorld.h"
#include "Scripting/ScriptEngine.h"
#include "Scripting/ScriptTypes.h"

namespace Vsp
{
	static constexpr const char* kLogTag = "GameEngine";

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

		// Key- and mouse-simulation bookkeeping (acceptance tests).
		size_t nNextKeyStepIndex = 0;
		size_t nNextMouseStepIndex = 0;
		uint32 uPendingKeyUpCode = 0;
		uint32 uKeyUpDueMilliseconds = 0;

		// High-resolution frame timer (all platform-specific timing code is
		// encapsulated in Common/PlatformMisc behind #if VSP_PLATFORM_WINDOWS).
		HighResolutionTimer FrameTimer;

		float TickFrameTimer()
		{
			return FrameTimer.Tick();
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
			// The clock is advanced FIRST, so everything that runs this frame -
			// the scripts below, the render pipeline and the frame-rate readout -
			// reads the same delta and the same elapsed time.
			//
			// The host measures; Classes/Time decides what the measurement means:
			// a fixed step replaces it in an acceptance run (which is what makes
			// the run reproducible), the stall guard clamps it, and the time
			// scale turns it into engine time.
			Time::Get().AdvanceFrame(TickFrameTimer());

			// Drive every managed script instance (Unity-style OnUpdate).
			ScriptEngine::Get().UpdateAllScripts();
		}

		// Advances the physics simulation by the frame's own delta time.
		//
		// The order inside a frame is what makes this a simulation rather than a
		// replay: the scripts run FIRST, so a velocity or a force they set
		// belongs to this frame's step, and the render flow runs LAST, so the
		// frame draws where the bodies ended up. A game can take the step over
		// (Physics.AutoSimulation = false) and call Physics.Step itself.
		void UpdatePhysics()
		{
			if (!PhysicsWorld::Get().IsAutoSimulationEnabled())
			{
				return;
			}

			PhysicsWorld::Get().Step(Time::Get().GetDeltaTime());
		}

		// Runs the managed render pipeline: it builds the frame's draw list
		// from the native scene and records every graphics command through the
		// wrapped graphics API; the Vulkan backend plays the recorded command
		// list back on RenderFrame.
		void RunRenderFlow()
		{
			ScriptEngine::Get().CallRenderFlow();
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

		// Destroy managed instances (and the scene objects they address)
		// before the runtime context goes away; the managed render pipeline
		// releases its graphics resources as part of that shutdown, which is
		// why the backend must still be alive here.
		ScriptEngine::Get().Shutdown();

		// Nothing can address the backend past this point.
		GraphicsSystem::Get().SetActiveBackend(nullptr);

		if (m_pImpl->pRenderer != nullptr)
		{
			m_pImpl->pRenderer->Shutdown();
			m_pImpl->pRenderer.reset();
		}

		// The pointer is given back before anything else goes: a cursor left hidden
		// or confined would follow the user out of the engine.
		InputManager& inputManager = InputManager::Get();
		inputManager.SetCursorMode(InputManager::CursorMode::Visible);
		inputManager.SetCursorWindowHandle(nullptr);

		// The physics world is a view over the scene, so it forgets what it
		// gathered before the scene goes.
		PhysicsWorld::Get().Clear();

		// The scene owns every native object the managed handles addressed; the
		// scripts are gone, so its tables go with them.
		Scene::Get().Clear();

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

		// The frame clock is configured before the first frame runs: a fixed step
		// is what makes an automated run reproducible, 0 keeps the wall clock.
		Time& frameClock = Time::Get();
		frameClock.ResetFrameClock();
		frameClock.SetFixedDeltaSeconds(config.fFixedDeltaSeconds);
		LOG_INFO(kLogTag, "Frame clock: {}.",
			frameClock.IsFixedTimeStep() ? "fixed step, reproducible run" : "wall clock");

		// The simulation is a view over the scene - it owns no objects of its own -
		// so all there is to report here is how it will run.
		LOG_INFO(kLogTag, "Physics world ready (gravity {} down, {} solver pass(es) per step, automatic step {}).",
			PhysicsWorld::Get().GetGravity().fY,
			PhysicsWorld::Get().GetSolverIterationCount(),
			PhysicsWorld::Get().IsAutoSimulationEnabled() ? "on" : "off");

		// The CoreCLR host needs DOTNET_ROOT pointing at the shipped runtime,
		// so nethost/get_hostfxr_path and hostpolicy resolve from there.
		PlatformMisc::SetEnvironmentVariableValue("DOTNET_ROOT", config.sDotNetRootPath);
		PlatformMisc::SetEnvironmentVariableValue("DOTNET_ROOT(x86)", config.sDotNetRootPath);

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

		// The input layer owns the pointer from here on: it is what hides it and
		// puts it back at the centre of the window while a script has the cursor
		// locked (see InputManager::CursorMode).
		InputManager& cursorInput = InputManager::Get();
		cursorInput.SetCursorWindowHandle(m_pImpl->pApplication->GetWindow()->GetNativeWindowHandle());
		cursorInput.SetCursorLockAllowed(config.bAllowCursorLock);

		// 2. Vulkan renderer (logs its own errors, including unsupported devices).
		m_pImpl->pRenderer = std::make_unique<VulkanRenderer2D>();
		if (!m_pImpl->pRenderer->Initialize(
			m_pImpl->pApplication->GetWindow()->GetNativeWindowHandle()))
		{
			outErrorText = "Failed to initialize the Vulkan renderer (see the engine log for details).";
			return false;
		}

		// The backend is now the target of every wrapped-graphics-API call the
		// managed render pipeline makes.
		GraphicsSystem::Get().SetActiveBackend(m_pImpl->pRenderer.get());

		// 3. CoreCLR script host: resolves the bridge from the engine
		//    assembly (VspEngine.dll), loads the game Assembly (Assembly.dll)
		//    and creates every script instance automatically.
		ScriptEngine& scriptEngine = ScriptEngine::Get();
		if (!scriptEngine.Initialize(
			config.sEngineAssemblyPath,
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

		// Every instance the host created gets its OnInit and its OnStart; the
		// first one is remembered as the primary because the frame-capture
		// diagnostics report its transform.
		m_pImpl->uPrimaryScriptInstanceId = scriptEngine.GetPrimaryScriptInstanceId();
		for (uint32 uIndex = 0; uIndex < scriptEngine.GetScriptInstanceCount(); ++uIndex)
		{
			const ScriptInstanceId uInstanceId = scriptEngine.GetScriptInstanceIdAt(uIndex);
			if (uInstanceId == 0)
			{
				// A script that failed to be created is already reported; the
				// engine keeps starting the others rather than dropping them.
				VSP_LOG_ERROR(kLogTag, "Script instance {} could not be started.", uIndex);
				continue;
			}

			scriptEngine.CallScriptInit(uInstanceId);
			scriptEngine.CallScriptStart(uInstanceId);
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

			// 1. The rendering system opens the frame: its command list starts empty
			//    and the input state the frame reads begins here. Window messages
			//    then flow into that state.
			GraphicsSystem::Get().BeginFrame();
			m_pImpl->pApplication->Update();

			if (!m_pImpl->pApplication->IsRunning())
			{
				break;
			}

			// 2. Scripts (C#) update, then the managed render flow builds
			//    the frame's render commands.
			m_pImpl->UpdateScripts();
			m_pImpl->UpdatePhysics();
			m_pImpl->RunRenderFlow();

			// 2b. Synthetic key simulation (acceptance tests): post real window
			// messages into the engine's own queue, so the whole input pipeline
			// runs exactly as it does for physical keys.
			{
				void* pWindowHandle =
					m_pImpl->pApplication->GetWindow()->GetNativeWindowHandle();
				// The scheduled steps are placed on the ENGINE clock, which is
				// the same clock the scripts read, so a reproducible run keeps
				// its schedule on any machine.
				const uint32 uElapsedMilliseconds =
					static_cast<uint32>(Time::Get().GetElapsedTime() * 1000.0f);

				ArrayList<GameEngineConfig::KeySimulationStep>& steps =
					m_pImpl->Config.KeySimulationSteps;
				while (m_pImpl->nNextKeyStepIndex < steps.GetSize() &&
					steps[m_pImpl->nNextKeyStepIndex].uStartMilliseconds <= uElapsedMilliseconds)
				{
					const GameEngineConfig::KeySimulationStep& step =
						steps[m_pImpl->nNextKeyStepIndex];
					LOG_INFO(kLogTag, "Simulating key down 0x{:X} at {} ms.", step.uVirtualKeyCode, uElapsedMilliseconds);
					PlatformMisc::PostWindowKeyMessage(pWindowHandle, step.uVirtualKeyCode, true);
					if (step.uHoldMilliseconds == 0)
					{
						PlatformMisc::PostWindowKeyMessage(pWindowHandle, step.uVirtualKeyCode, false);
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
					PlatformMisc::PostWindowKeyMessage(pWindowHandle, m_pImpl->uPendingKeyUpCode, false);
					m_pImpl->uPendingKeyUpCode = 0;
				}

				// Synthetic mouse movement: a pointer position per scheduled
				// step, posted through the same window message a physical mouse
				// produces, so the whole input path runs unchanged.
				ArrayList<GameEngineConfig::MouseSimulationStep>& mouseSteps =
					m_pImpl->Config.MouseSimulationSteps;
				while (m_pImpl->nNextMouseStepIndex < mouseSteps.GetSize() &&
					mouseSteps[m_pImpl->nNextMouseStepIndex].uStartMilliseconds <= uElapsedMilliseconds)
				{
					const GameEngineConfig::MouseSimulationStep& mouseStep =
						mouseSteps[m_pImpl->nNextMouseStepIndex];
					LOG_INFO(kLogTag, "Simulating mouse move to ({}, {}) at {} ms.",
						mouseStep.nClientX, mouseStep.nClientY, uElapsedMilliseconds);
					PlatformMisc::PostWindowMouseMoveMessage(pWindowHandle, mouseStep.nClientX, mouseStep.nClientY);
					++m_pImpl->nNextMouseStepIndex;
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
					float fPositionZ = 0.0f;
					const Transform* pPrimaryTransform = Scene::Get().FindTransform(
						Scene::Get().FindGameObjectTransformHandle(
							ScriptEngine::Get().GetPrimaryScriptGameObjectHandle()));
					if (pPrimaryTransform != nullptr)
					{
						pPrimaryTransform->GetLocalPosition(fPositionX, fPositionY, fPositionZ);
					}

					LOG_INFO(kLogTag, "Capture at frame {}: script position={:p}, deltaTime={}, elapsed={} ms.",
						m_pImpl->uFrameCount, Position2D{ fPositionX, fPositionY },
						Time::Get().GetDeltaTime(),
						static_cast<uint32>(Time::Get().GetElapsedTime() * 1000.0f));

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

			// 5. Frame bookkeeping: the rendering system closes the frame, which is
			//    what clears the input edges and the mouse movement the frame
			//    accumulated.
			GraphicsSystem::Get().EndFrame();
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
