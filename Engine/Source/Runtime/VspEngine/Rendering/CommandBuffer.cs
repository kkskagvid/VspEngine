using System;
using System.Runtime.InteropServices;

namespace VspEngine.Rendering
{
	/// <summary>
	/// Records the graphics commands of one frame. Every call forwards straight
	/// into the native command list; the native backend plays the recorded
	/// commands back while it records the swapchain command buffer, so recording
	/// is cheap and nothing is submitted before <see cref="Submit"/>.
	/// </summary>
	public sealed class CommandBuffer
	{
		// Scratch buffer push-constant blocks are marshalled through, reused so
		// recording a frame does not allocate.
		private const int PushConstantScratchByteCount = 64;
		private readonly byte[] pushConstantScratch = new byte[PushConstantScratchByteCount];

		internal CommandBuffer() { }

		/// <summary>
		/// Starts the frame's render pass, clearing the render target to the
		/// given color first.
		/// </summary>
		public void BeginRenderPass(Color clearColor)
		{
			RhiApi.VspRhi_SetClearColor(clearColor.R, clearColor.G, clearColor.B, clearColor.A);
			RhiApi.VspRhi_CmdBeginRenderPass();
		}

		/// <summary>Ends the frame's render pass.</summary>
		public void EndRenderPass() => RhiApi.VspRhi_CmdEndRenderPass();

		/// <summary>Sets the viewport rectangle in pixels.</summary>
		public void SetViewport(float x, float y, float width, float height) =>
			RhiApi.VspRhi_CmdSetViewport(x, y, width, height);

		/// <summary>Sets the scissor rectangle in pixels.</summary>
		public void SetScissor(int x, int y, uint width, uint height) =>
			RhiApi.VspRhi_CmdSetScissor(x, y, width, height);

		/// <summary>Binds the pipeline the following draws use.</summary>
		public void BindPipeline(GraphicsPipeline pipeline)
		{
			if (pipeline == null)
			{
				throw new ArgumentNullException(nameof(pipeline));
			}

			RhiApi.VspRhi_CmdBindPipeline(pipeline.NativeHandle);
		}

		/// <summary>Binds the vertex buffer the following draws read from.</summary>
		public void BindVertexBuffer(VertexBuffer vertexBuffer)
		{
			if (vertexBuffer == null)
			{
				throw new ArgumentNullException(nameof(vertexBuffer));
			}

			RhiApi.VspRhi_CmdBindVertexBuffer(vertexBuffer.NativeHandle);
		}

		/// <summary>
		/// Sets the push-constant block of the bound pipeline for the following
		/// draws. The block layout is the one the shaders declare.
		/// </summary>
		public void PushConstants<TValue>(ShaderStageFlags stageFlags, in TValue value) where TValue : struct
		{
			int byteCount = Marshal.SizeOf<TValue>();
			if (byteCount <= 0 || byteCount > PushConstantScratchByteCount)
			{
				Debug.LogError("Rendering.CommandBuffer: a push-constant block of " + byteCount + " bytes is out of range.");
				return;
			}

			MemoryMarshal.Write(pushConstantScratch.AsSpan(), in value);
			RhiApi.VspRhi_CmdPushConstants((int)stageFlags, 0, pushConstantScratch, (uint)byteCount);
		}

		/// <summary>Records one non-indexed draw.</summary>
		public void Draw(uint vertexCount, uint firstVertex = 0) =>
			RhiApi.VspRhi_CmdDraw(vertexCount, firstVertex);

		/// <summary>
		/// Closes the frame's command list. The render pipeline manager calls it
		/// through <see cref="ScriptableRenderContext.Submit"/>; a frame that was
		/// never closed is closed for the pipeline when it returns.
		/// </summary>
		internal void Submit() => RhiApi.VspRhi_EndFrame();
	}
}
