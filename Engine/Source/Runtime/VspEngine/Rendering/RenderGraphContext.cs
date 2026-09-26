using System;

namespace VspEngine.Rendering
{
	/// <summary>
	/// What a pass is handed while it records: the frame's command buffer, the
	/// size of the render target, the name of the pass being recorded and the one
	/// piece of render-graph knowledge a pass cannot look up by itself - which
	/// bindless slot the texture it declared as a read ended up in.
	///
	/// One context instance serves every pass of one <see cref="RenderGraph"/> and
	/// is re-pointed at each pass right before its callback runs, so a pass must
	/// use it inside the callback and must not keep it: <see cref="PassName"/> and
	/// the back-buffer size describe the pass that is recording NOW. That reuse is
	/// deliberate - a graph is rebuilt and executed every frame, and the
	/// alternative would be one throwaway object per pass per frame.
	///
	/// The context is only a view. It does not submit the frame (the pipeline
	/// manager owns that), and it does not open or close the backend's render
	/// pass: a pass that renders into the back buffer does that itself through
	/// <see cref="CommandBuffer.BeginRenderPass"/> and
	/// <see cref="CommandBuffer.EndRenderPass"/>, exactly as the backend requires.
	/// </summary>
	public sealed class RenderGraphContext
	{
		private readonly RenderGraph graph;
		private readonly ScriptableRenderContext scriptableRenderContext;
		private RenderGraphPass? currentPass;
		private string passName = string.Empty;
		private int backbufferWidth;
		private int backbufferHeight;

		internal RenderGraphContext(RenderGraph graph, ScriptableRenderContext scriptableRenderContext)
		{
			this.graph = graph;
			this.scriptableRenderContext = scriptableRenderContext;
		}

		/// <summary>The command buffer the recording pass forwards its commands into.</summary>
		public CommandBuffer CommandBuffer => scriptableRenderContext.CommandBuffer;

		/// <summary>
		/// The frame's context: the gathered draw list, the render target size and
		/// <see cref="ScriptableRenderContext.Submit"/> for a pipeline that wants
		/// to close the frame early.
		/// </summary>
		public ScriptableRenderContext ScriptableRenderContext => scriptableRenderContext;

		/// <summary>Width of the back buffer in pixels, as the native host reported it this frame.</summary>
		public int BackbufferWidth => backbufferWidth;

		/// <summary>Height of the back buffer in pixels, as the native host reported it this frame.</summary>
		public int BackbufferHeight => backbufferHeight;

		/// <summary>Name of the pass that is recording; empty outside a pass callback.</summary>
		public string PassName => passName;

		/// <summary>
		/// Resolves a resource handle into the slot of the bindless sampled image
		/// array a shader reads it from, which is the value a pipeline writes into
		/// its push constants.
		///
		/// Only handles the current pass declared as reads resolve; a handle the
		/// pass did not declare, an invalid handle and a resource that has no
		/// texture yet all answer -1. This is a query, not a failure, so nothing
		/// is logged: a pass that asks for a texture it never declared has simply
		/// asked for nothing.
		/// </summary>
		public int GetTextureBindlessSlot(RenderGraphTextureHandle handle)
		{
			if (currentPass == null || !handle.IsValid || !currentPass.DeclaresRead(handle))
			{
				return -1;
			}
			return graph.GetResourceBindlessSlot(handle.Index);
		}

		/// <summary>Points the context at the pass that is about to record.</summary>
		internal void BeginPass(RenderGraphPass pass, int width, int height)
		{
			currentPass = pass;
			passName = pass.Name;
			backbufferWidth = width;
			backbufferHeight = height;
		}

		/// <summary>Drops the pass that just recorded, whether it returned or threw.</summary>
		internal void EndPass()
		{
			currentPass = null;
			passName = string.Empty;
		}
	}
}
