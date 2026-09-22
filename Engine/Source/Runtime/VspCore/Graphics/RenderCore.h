#pragma once

#include "Core/Core.h"
#include "Core/Templates/ArrayList.h"
#include "Graphics/RhiTypes.h"

namespace Vsp
{
	// -------------------------------------------------------------------------
	// RenderCore
	// -------------------------------------------------------------------------
	// The frame command list of the wrapped graphics API. The managed render
	// pipeline records one frame per engine frame through the NativeExports
	// (DllImport("VspCore")); the active graphics backend plays the recorded
	// commands back while it records the swapchain command buffer.
	//
	// One frame = BeginFrame -> SetClearColor -> commands -> EndFrame. The
	// backend only trusts a frame that was opened and closed in order and falls
	// back to a default clear otherwise, so a broken pipeline cannot wedge the
	// window.
	//
	// All functions are plain data operations on the command array; nothing
	// throws and nothing allocates outside it.
	// -------------------------------------------------------------------------
#pragma warning(push)
#pragma warning(disable : 4251)   // ArrayList member: header-only template.
	class RUNTIME_API RenderCore
	{
	public:
		// Frame background clear color (RGBA).
		struct FrameClearColor
		{
			float fColorR = 0.06f;
			float fColorG = 0.06f;
			float fColorB = 0.10f;
			float fColorA = 1.0f;
		};

		static RenderCore& Get();

		// -------- Frame lifetime --------
		// Opens a new frame: the previous frame's commands are discarded.
		void BeginFrame();

		// Closes the frame; the backend may now play the command list back.
		void EndFrame();

		// Discards the recorded commands without closing the frame.
		void ResetCommands();

		// True when the current frame was opened and closed in order.
		bool IsFrameValid() const { return m_bFrameBegun && m_bFrameEnded; }

		// True while a frame is open (commands recorded so far belong to it).
		bool IsFrameOpen() const { return m_bFrameBegun && !m_bFrameEnded; }

		const FrameClearColor& GetClearColor() const { return m_ClearColor; }
		void SetClearColor(float fColorR, float fColorG, float fColorB, float fColorA);

		// -------- Camera --------
		// The camera the frame is rendered from. Like the clear color this is
		// frame state rather than a command: the render pipeline sets it while it
		// records, and the backend reads it when it fills the camera uniform
		// buffer. A frame nobody set a camera for keeps the previous one (and the
		// backend's default before that).
		const RhiFrameCamera& GetFrameCamera() const { return m_FrameCamera; }
		void SetFrameCamera(const RhiFrameCamera& frameCamera) { m_FrameCamera = frameCamera; }

		// -------- Command recording --------
		void BeginRenderPass();
		void EndRenderPass();
		void SetViewport(float fX, float fY, float fWidth, float fHeight);
		void SetScissor(int32 nX, int32 nY, uint32 uWidth, uint32 uHeight);
		void BindPipeline(RhiPipelineHandle uPipeline);
		void BindVertexBuffer(RhiBufferHandle uVertexBuffer);
		void BindIndexBuffer(RhiBufferHandle uIndexBuffer);
		void PushConstants(uint32 uShaderStageFlags, uint32 uByteOffset, const void* pData, uint32 uByteCount);
		void Draw(uint32 uVertexCount, uint32 uFirstVertex);
		void DrawIndexed(uint32 uIndexCount, uint32 uFirstIndex, uint32 uFirstVertex);

		const ArrayList<RhiCommand>& GetCommands() const { return m_Commands; }

	private:
		RenderCore() = default;

		// Appends one command, dropping it (with a single warning) once the
		// frame reached k_nMaxRecordedCommandCount.
		RhiCommand& AddCommand(RhiCommandType eType);

		bool m_bFrameBegun = false;
		bool m_bFrameEnded = false;
		bool m_bCommandLimitReported = false;
		FrameClearColor m_ClearColor;
		RhiFrameCamera m_FrameCamera;
		ArrayList<RhiCommand> m_Commands;

		// Receives commands recorded past k_nMaxRecordedCommandCount so an
		// over-long frame degrades instead of corrupting the list.
		RhiCommand m_DiscardedCommand;
	};
#pragma warning(pop)
}
