using System.Collections.Generic;
using System.Numerics;

namespace VspEngine.Rendering
{
	/// <summary>
	/// One drawable the render pipeline may submit: the script component that
	/// requested rendering, its transform and the material it draws with. The
	/// list is built once per frame from the native scene, so a pipeline never
	/// walks the scene itself.
	///
	/// The engine reports where a drawable IS and what it draws WITH; what that
	/// means for the pixels is a property of the material and therefore entirely
	/// the game's business.
	/// </summary>
	public readonly struct RenderDrawItem
	{
		/// <summary>Native handle of the component that wants to be drawn.</summary>
		public readonly uint ComponentHandle;

		/// <summary>Native handle of that component's transform.</summary>
		public readonly uint TransformHandle;

		/// <summary>Position of the transform.</summary>
		public readonly Vector3 Position;

		/// <summary>Material the component draws with (0 when it has none yet).</summary>
		public readonly uint MaterialHandle;

		public RenderDrawItem(
			uint componentHandle,
			uint transformHandle,
			Vector3 position,
			uint materialHandle)
		{
			ComponentHandle = componentHandle;
			TransformHandle = transformHandle;
			Position = position;
			MaterialHandle = materialHandle;
		}

		/// <summary>
		/// The material the component draws with, or null before a pipeline gave
		/// it one.
		/// </summary>
		public Material? Material => MaterialHandle != 0 ? new Material(MaterialHandle) : null;

		/// <summary>
		/// Makes the drawable render with the given material from the next frame
		/// on. A pipeline calls this when it hands a drawable its default.
		/// </summary>
		public void AssignMaterial(Material material)
		{
			if (material == null)
			{
				throw new System.ArgumentNullException(nameof(material));
			}
			NativeApi.VspComponent_SetMaterial(ComponentHandle, material.NativeHandle);
		}
	}

	/// <summary>
	/// Everything a render pipeline gets for one frame: the command buffer it
	/// records into, the draw list gathered from the scene and the size of the
	/// render target.
	///
	/// The pipeline records into <see cref="CommandBuffer"/> and calls
	/// <see cref="Submit"/> when the frame is complete; the pipeline manager
	/// closes the frame for a pipeline that forgot to.
	/// </summary>
	public sealed class ScriptableRenderContext
	{
		private readonly List<RenderDrawItem> drawItems = new List<RenderDrawItem>();

		internal ScriptableRenderContext()
		{
			CommandBuffer = new CommandBuffer();
		}

		/// <summary>The command buffer the pipeline records this frame into.</summary>
		public CommandBuffer CommandBuffer { get; }

		/// <summary>Drawables gathered from the native scene for this frame.</summary>
		public IReadOnlyList<RenderDrawItem> DrawItems => drawItems;

		/// <summary>Width of the render target in pixels.</summary>
		public int BackbufferWidth { get; private set; }

		/// <summary>Height of the render target in pixels.</summary>
		public int BackbufferHeight { get; private set; }

		/// <summary>True once the pipeline closed the frame.</summary>
		public bool HasSubmitted { get; private set; }

		/// <summary>Closes the frame's command list; the backend presents it.</summary>
		public void Submit()
		{
			if (!HasSubmitted)
			{
				CommandBuffer.Submit();
				HasSubmitted = true;
			}
		}

		/// <summary>Starts a new frame with the draw list gathered from the scene.</summary>
		internal void BeginFrame(List<RenderDrawItem> frameDrawItems, int backbufferWidth, int backbufferHeight)
		{
			drawItems.Clear();
			drawItems.AddRange(frameDrawItems);
			BackbufferWidth = backbufferWidth;
			BackbufferHeight = backbufferHeight;
			HasSubmitted = false;
		}
	}
}
