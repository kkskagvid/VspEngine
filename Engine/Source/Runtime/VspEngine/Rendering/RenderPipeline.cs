using System;

namespace VspEngine.Rendering
{
	/// <summary>
	/// Base class of every render pipeline. A pipeline owns the rendering flow of
	/// a frame: it decides which passes run, which state they set and what they
	/// draw, using only the wrapped graphics API through the command buffer of
	/// the context it is handed.
	///
	/// The active pipeline is chosen by <see cref="RenderPipelineManager"/>; the
	/// engine installs a default one at startup, and a game assembly replaces it
	/// whenever it wants its own flow.
	/// </summary>
	public abstract class RenderPipeline : IDisposable
	{
		/// <summary>Builds and records the frame.</summary>
		public abstract void Render(ScriptableRenderContext context);

		/// <summary>Releases the graphics resources the pipeline created.</summary>
		public virtual void Dispose() { }
	}
}
