using System.Numerics;

namespace VspEngine.Rendering
{
	/// <summary>
	/// The engine's managed render flow. The native host invokes
	/// <see cref="Execute"/> every frame right after the scripts update; the
	/// flow builds the frame (clear color + triangle draws) through the
	/// native render-command API exposed by VspCore (DllImport("VspCore")).
	/// The native Vulkan renderer consumes the submitted commands when it
	/// records the frame's command buffer.
	/// </summary>
	public static class RenderFlow
	{
		/// <summary>
		/// Builds one frame: clears to the demo background color and draws the
		/// triangle owned by the primary script instance at its current
		/// transform with its current color mode.
		/// </summary>
		/// <param name="primaryInstanceId">InstanceID of the primary script (0 = none).</param>
		public static void Execute(uint primaryInstanceId)
		{
			Renderer.BeginFrame();
			Renderer.SetClearColor(0.06f, 0.06f, 0.10f, 1.0f);

			if (primaryInstanceId != 0)
			{
				Vector2 position = Renderer.GetInstancePosition(primaryInstanceId);
				Renderer.DrawTriangle(position.X, position.Y, Renderer.GetColorMode(primaryInstanceId));
			}

			Renderer.EndFrame();
		}
	}
}
