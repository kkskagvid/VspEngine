using VspEngine;

namespace Assembly.Rendering
{
	/// <summary>Colors the demo triangle can show.</summary>
	public enum TriangleColorMode
	{
		Red = 0,
		Blue = 1,
		Green = 2,
		MultiColor = 3,
	}

	/// <summary>
	/// The shader properties this game drives, and the small helpers that read
	/// and write them on a component's material.
	///
	/// The names live here, in the game, because they are written in the game's
	/// shader (Assembly/Shaders/Triangle2D.vsf): the engine neither knows nor
	/// cares that "_ColorMode" exists.
	/// </summary>
	public static class TriangleMaterial
	{
		/// <summary>Property that selects which color the fragment stage shows.</summary>
		public const string ColorModePropertyName = "_ColorMode";

		/// <summary>Property that tints whatever the shader outputs.</summary>
		public const string TintPropertyName = "_Tint";

		/// <summary>
		/// Sets the color mode a component is drawn with. A component that has no
		/// material yet keeps the material the pipeline hands it on the first
		/// frame, so this returns false until then.
		/// </summary>
		public static bool SetColorMode(Component component, TriangleColorMode colorMode)
		{
			Material? material = Renderer.GetMaterial(component);
			return material != null && material.SetFloat(ColorModePropertyName, (float)colorMode);
		}

		/// <summary>Color mode the component's material currently selects.</summary>
		public static TriangleColorMode GetColorMode(Component component)
		{
			Material? material = Renderer.GetMaterial(component);
			if (material == null)
			{
				return TriangleColorMode.MultiColor;
			}
			return (TriangleColorMode)(int)material.GetFloat(ColorModePropertyName, (float)TriangleColorMode.MultiColor);
		}

		/// <summary>The color the given mode overrides the vertex colors with.</summary>
		public static void GetOverrideColor(TriangleColorMode colorMode, out float red, out float green, out float blue)
		{
			red = colorMode == TriangleColorMode.Red ? 1.0f : 0.0f;
			green = colorMode == TriangleColorMode.Green ? 1.0f : 0.0f;
			blue = colorMode == TriangleColorMode.Blue ? 1.0f : 0.0f;
		}
	}
}
