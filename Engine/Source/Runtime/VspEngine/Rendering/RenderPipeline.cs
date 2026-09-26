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
		/// <summary>
		/// The camera this pipeline renders the frame from, or null for a frame
		/// nobody looks at.
		///
		/// The engine asks for it BEFORE it opens the frame, because the frame needs
		/// it: the camera block a shader reads is filled from it, and the sky is
		/// painted with it. A pipeline that creates its resources on demand is
		/// therefore asked once before its first Render - which is exactly when it
		/// should build them.
		/// </summary>
		public virtual Camera? Camera => null;

		/// <summary>Builds and records the frame.</summary>
		public abstract void Render(ScriptableRenderContext context);

		/// <summary>Releases the graphics resources the pipeline created.</summary>
		public virtual void Dispose() { }
	}
}
