#pragma once

#include <memory>

#include "Core/Core.h"
#include "Core/String/VspString.h"
#include "Core/Templates/ArrayList.h"
#include "Core/Window.h"

namespace Vsp
{
	// -------------------------------------------------------------------------
	// GameEngineConfig
	// -------------------------------------------------------------------------
	// Everything the engine host needs at startup.
	// -------------------------------------------------------------------------
	struct GameEngineConfig
	{
		VspString sWindowTitle = "Vsp Engine";
		uint32 uWindowWidth = 1280;
		uint32 uWindowHeight = 720;

		// Engine's managed runtime assembly (VspEngine.dll): hosts the
		// interop bridge, the script base types and the managed render flow.
		VspString sEngineAssemblyPath;

		// Game Assembly (Assembly.dll): the assembly holding the user scripts.
		VspString sAssemblyPath;

		// runtimeconfig.json used by hostfxr (Launch.runtimeconfig.json).
		VspString sRuntimeConfigPath;

		// Directory containing host\ and shared\ (the shipped .NET runtime).
		VspString sDotNetRootPath;

		// 0 = run until the window closes; > 0 = exit after N frames (smoke tests).
		uint32 uMaxFrameCount = 0;

		// 0 = advance the frame clock with the wall clock. A positive value
		// advances it by exactly this many seconds every frame instead, which
		// makes an automated run independent of how fast the machine renders:
		// frame N always happens at N * fFixedDeltaSeconds of engine time, so
		// the scheduled key presses and captures line up on every machine.
		float fFixedDeltaSeconds = 0.0f;

		// Automated acceptance-test hooks --------------------------------------
		// Posts synthetic WM_KEYDOWN/WM_KEYUP messages into the engine's own
		// window, exercising the full input pipeline (Win32 -> events ->
		// InputManager -> C# script).
		struct KeySimulationStep
		{
			uint32 uVirtualKeyCode = 0;      // Win32 VK_* value
			uint32 uHoldMilliseconds = 0;    // 0 = tap (down immediately followed by up)
			uint32 uStartMilliseconds = 0;   // Elapsed-time offset from loop start.
		};
		ArrayList<KeySimulationStep> KeySimulationSteps;

		// Posts synthetic WM_MOUSEMOVE messages that put the pointer at a client
		// coordinate, which is how a camera that reads the mouse delta is
		// exercised without a hand on the mouse. Deltas come from successive
		// positions, so the steps are posted in order.
		struct MouseSimulationStep
		{
			int32 nClientX = 0;
			int32 nClientY = 0;
			uint32 uStartMilliseconds = 0;         // Elapsed-time offset from loop start.
			bool bHasExplicitStartTime = false;    // True when the step pinned its own offset.
		};
		ArrayList<MouseSimulationStep> MouseSimulationSteps;

		// Saves the presented framebuffer (BMP) after the given frame index.
		struct FrameCapture
		{
			uint32 uFrameIndex = UINT32_MAX;
			VspString sFilePath;
		};
		ArrayList<FrameCapture> FrameCaptures;
	};

	// -------------------------------------------------------------------------
	// GameEngine
	// -------------------------------------------------------------------------
	// The small experimental engine: one window, a Vulkan 2D renderer and the
	// CoreCLR script host. Initialize() wires everything up and reports errors
	// through outErrorText (never throws); Run() drives the main loop:
	//   window messages -> input -> C# OnUpdate -> render -> end of frame.
	// The implementation is hidden behind pImpl so engine headers never leak
	// Vulkan / hosting includes into the host executable.
	// -------------------------------------------------------------------------
#pragma warning(push)
#pragma warning(disable : 4251)   // pImpl unique_ptr: incomplete type lives in the .cpp.
	class RUNTIME_API GameEngine
	{
	public:
		GameEngine();
		~GameEngine();

		bool Initialize(const GameEngineConfig& config, VspString& outErrorText);
		void Run();
		void RequestExit();
		bool IsRunning() const;
		uint32 GetFrameCount() const;

	private:
		struct Impl;
		std::unique_ptr<Impl> m_pImpl;
	};
#pragma warning(pop)
}
