using VspEngine.Rendering;

namespace VspEngine
{
	/// <summary>Triangle color modes. The demo cycles Red -> Blue -> Green -> MultiColor.</summary>
	public enum ColorMode
	{
		Red = 0,
		Blue = 1,
		Green = 2,
		MultiColor = 3,
	}

	/// <summary>
	/// Per-object render state. The values live in the native scene (on the
	/// component), so the render pipeline can read them while it builds the
	/// frame without calling back into script code.
	/// </summary>
	public static class Renderer
	{
		/// <summary>Sets the color mode the render pipeline draws this component with.</summary>
		public static void SetColorMode(Component component, ColorMode mode) =>
			NativeApi.VspComponent_SetColorMode(component.NativeHandle, (int)mode);

		/// <summary>Reads the color mode the render pipeline draws this component with.</summary>
		public static ColorMode GetColorMode(Component component) =>
			(ColorMode)NativeApi.VspComponent_GetColorMode(component.NativeHandle);

		/// <summary>Whether this component contributes a triangle to the frame.</summary>
		public static bool IsRenderable(Component component) =>
			NativeApi.VspComponent_IsRenderable(component.NativeHandle) != 0;

		/// <summary>Makes the component take part in (or drop out of) the frame.</summary>
		public static void SetRenderable(Component component, bool isRenderable) =>
			NativeApi.VspComponent_SetRenderable(component.NativeHandle, isRenderable ? 1 : 0);
	}
}
