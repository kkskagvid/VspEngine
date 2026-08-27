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
		uint32_t uWindowWidth = 1280;
		uint32_t uWindowHeight = 720;

		// Managed game assembly (VspPlayer.dll).
		VspString sAssemblyPath;

		// runtimeconfig.json used by hostfxr (Launch.runtimeconfig.json).
		VspString sRuntimeConfigPath;

		// Directory containing host\ and shared\ (the shipped .NET runtime).
		VspString sDotNetRootPath;

		// 0 = run until the window closes; > 0 = exit after N frames (smoke tests).
		uint32_t uMaxFrameCount = 0;

		// Automated acceptance-test hooks --------------------------------------
		// Posts synthetic WM_KEYDOWN/WM_KEYUP messages into the engine's own
		// window, exercising the full input pipeline (Win32 -> events ->
		// InputManager -> C# script).
		struct KeySimulationStep
		{
			uint32_t uVirtualKeyCode = 0;      // Win32 VK_* value
			uint32_t uHoldMilliseconds = 0;    // 0 = tap (down immediately followed by up)
			uint32_t uStartMilliseconds = 0;   // Elapsed-time offset from loop start.
		};
		ArrayList<KeySimulationStep> KeySimulationSteps;

		// Saves the presented framebuffer (BMP) after the given frame index.
		struct FrameCapture
		{
			uint32_t uFrameIndex = UINT32_MAX;
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
		uint32_t GetFrameCount() const;

	private:
		struct Impl;
		std::unique_ptr<Impl> m_pImpl;
	};
#pragma warning(pop)
}
