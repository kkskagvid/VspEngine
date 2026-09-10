using System.Numerics;

namespace VspEngine
{
	/// <summary>Triangle color modes. The T key cycles Red -> Blue -> Green -> MultiColor.</summary>
	public enum ColorMode
	{
		Red = 0,
		Blue = 1,
		Green = 2,
		MultiColor = 3,
	}

	/// <summary>
	/// Unity-style Renderer facade forwarded to the native engine. The frame
	/// render flow (BeginFrame -> clear -> draws -> EndFrame) is implemented
	/// by Rendering.RenderFlow and driven by the native host every frame.
	/// </summary>
	public static class Renderer
	{
		public static void SetColorMode(uint instanceId, ColorMode mode) =>
			NativeApi.VspRenderer_SetColorMode(instanceId, (int)mode);

		public static ColorMode GetColorMode(uint instanceId) =>
			(ColorMode)NativeApi.VspRenderer_GetColorMode(instanceId);

		/// <summary>World-space position of the script instance (screen center is the origin).</summary>
		public static Vector2 GetInstancePosition(uint instanceId) =>
			new Vector2(
				NativeApi.VspTransform_GetPositionX(instanceId),
				NativeApi.VspTransform_GetPositionY(instanceId));

		// -------- Frame render flow (issued by Rendering.RenderFlow) --------

		/// <summary>Starts a new frame: discards the previous frame's commands.</summary>
		public static void BeginFrame() => NativeApi.VspRenderer_BeginFrame();

		/// <summary>Sets the clear color used when the frame begins.</summary>
		public static void SetClearColor(float red, float green, float blue, float alpha) =>
			NativeApi.VspRenderer_SetClearColor(red, green, blue, alpha);

		/// <summary>Queues one triangle draw at the given world position.</summary>
		public static void DrawTriangle(float positionX, float positionY, ColorMode mode) =>
			NativeApi.VspRenderer_DrawTriangle(positionX, positionY, (int)mode);

		/// <summary>Closes the frame command list; the native renderer records it.</summary>
		public static void EndFrame() => NativeApi.VspRenderer_EndFrame();
	}
}
