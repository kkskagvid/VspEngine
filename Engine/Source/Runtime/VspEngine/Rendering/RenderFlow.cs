namespace VspEngine.Rendering
{
	/// <summary>
	/// The native host's entry point into managed rendering: it runs the active
	/// programmable render pipeline (see <see cref="RenderPipelineManager"/>)
	/// once per frame.
	///
	/// The flow itself - which passes run, which state they set, what they draw -
	/// belongs entirely to the pipeline; the native layer only provides the
	/// wrapped graphics API the pipeline records through.
	/// </summary>
	public static class RenderFlow
	{
		/// <summary>Runs one frame of the active render pipeline.</summary>
		public static void Execute()
		{
			RenderPipelineManager.Render();
		}

		/// <summary>Lets the active pipeline release its graphics resources.</summary>
		public static void Release()
		{
			RenderPipelineManager.ReleaseActivePipeline();
		}
	}
}
