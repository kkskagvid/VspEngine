#pragma once

#include "Core/Core.h"
#include "Core/Templates/ArrayList.h"

namespace Vsp
{
	// -------------------------------------------------------------------------
	// RenderCore
	// -------------------------------------------------------------------------
	// Native side of the frame render flow. The managed VspEngine render flow
	// (VspEngine.Rendering.RenderFlow) submits the frame's render commands
	// every frame through the NativeExports (DllImport("VspCore")); the Vulkan
	// renderer consumes the collected commands when it records the frame's
	// command buffer. One frame = BeginFrame -> commands -> EndFrame; the
	// renderer only trusts a frame that was opened and closed in order.
	// All functions are plain data operations - nothing throws.
	// -------------------------------------------------------------------------
#pragma warning(push)
#pragma warning(disable : 4251)   // ArrayList member: header-only template.
	class RUNTIME_API RenderCore
	{
	public:
		// One queued triangle draw: world position + color mode.
		struct TriangleDrawCommand
		{
			float fPositionX = 0.0f;
			float fPositionY = 0.0f;
			int32 nColorMode = 3;   // MultiColor
		};

		// Frame background clear color (RGBA).
		struct FrameClearColor
		{
			float fColorR = 0.06f;
			float fColorG = 0.06f;
			float fColorB = 0.10f;
			float fColorA = 1.0f;
		};

		static constexpr uint32 k_nMaxDrawCommandCount = 64;

		static RenderCore& Get();

		// Opens a new frame: discards the previous frame's commands.
		void BeginFrame();

		// Sets the clear color used when the frame starts.
		void SetClearColor(float fColorR, float fColorG, float fColorB, float fColorA);

		// Queues one triangle draw at the given world position. Commands past
		// k_nMaxDrawCommandCount are dropped (a warning is logged once).
		void DrawTriangle(float fPositionX, float fPositionY, int32 nColorMode);

		// Closes the frame: the command list becomes consumable.
		void EndFrame();

		// True when the current frame was opened and closed in order.
		bool IsFrameValid() const { return m_bFrameBegun && m_bFrameEnded; }

		const FrameClearColor& GetClearColor() const { return m_ClearColor; }
		const ArrayList<TriangleDrawCommand>& GetTriangleDrawCommands() const { return m_DrawCommands; }

	private:
		RenderCore() = default;

		bool m_bFrameBegun = false;
		bool m_bFrameEnded = false;
		FrameClearColor m_ClearColor;
		ArrayList<TriangleDrawCommand> m_DrawCommands;
	};
#pragma warning(pop)
}
