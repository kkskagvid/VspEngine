namespace VspEngine.Rendering
{
	/// <summary>
	/// The engine's frame driver, as the native host reaches it: the one managed
	/// entry point that renders A FRAME (see <see cref="RenderPipelineManager"/>).
	///
	/// The host calls <see cref="RenderFrame"/> once per engine frame, after the
	/// scripts and the physics step, and <see cref="ReleasePipelineResources"/>
	/// once at shutdown, while the graphics backend is still alive.
	///
	/// Which passes run, which state they set and what they draw belongs entirely
	/// to the ACTIVE PIPELINE, and the FRAME those passes are recorded inside -
	/// its render pass, its clear, its sky and its interface - belongs to the
	/// engine. The native layer also provides the wrapped graphics API the
	/// pipeline records through.
	/// </summary>
	public static class RenderFrameDriver
	{
		/// <summary>
		/// Renders one frame of the active render pipeline: the engine's frame
		/// (render pass, clear, sky, interface) with the active pipeline's passes
		/// inside it.
		/// </summary>
		public static void RenderFrame()
		{
			RenderPipelineManager.Render();
		}

		/// <summary>
		/// Lets the active pipeline release the graphics resources it created, and
		/// releases the ones the engine's own frame owns.
		/// </summary>
		public static void ReleasePipelineResources()
		{
			RenderPipelineManager.ReleaseActivePipeline();
		}
	}
}
