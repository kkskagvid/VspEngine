namespace VspEngine
{
	/// <summary>Triangle color modes. The demo cycles Red -&gt; Blue -&gt; Green -&gt; MultiColor.</summary>
	public enum ColorMode
	{
		Red = 0,
		Blue = 1,
		Green = 2,
		MultiColor = 3,
	}

	/// <summary>
	/// Per-object render state. The values live in the native scene (on the
	/// component and on the material it points at), so the render pipeline can
	/// read them while it builds the frame without calling back into script code.
	/// </summary>
	public static class Renderer
	{
		/// <summary>
		/// The material this component draws with. The render pipeline gives
		/// every renderable component a default material on the first frame, so
		/// this returns null only before that happened.
		/// </summary>
		public static Material? GetMaterial(Component component)
		{
			uint materialHandle = NativeApi.VspComponent_GetMaterial(component.NativeHandle);
			return materialHandle != 0 ? new Material(materialHandle) : null;
		}

		/// <summary>Makes the component draw with the given material.</summary>
		public static void SetMaterial(Component component, Material material)
		{
			if (material == null)
			{
				throw new System.ArgumentNullException(nameof(material));
			}
			NativeApi.VspComponent_SetMaterial(component.NativeHandle, material.NativeHandle);
		}

		/// <summary>
		/// Sets the color mode the render pipeline draws this component with.
		/// The value lives on the component's material; a component that has none
		/// yet keeps it until the pipeline hands it the default material.
		/// </summary>
		public static void SetColorMode(Component component, ColorMode mode)
		{
			NativeApi.VspComponent_SetColorMode(component.NativeHandle, (int)mode);

			Material? material = GetMaterial(component);
			material?.SetFloat(Material.ColorModePropertyName, (float)mode);
		}

		/// <summary>Reads the color mode the render pipeline draws this component with.</summary>
		public static ColorMode GetColorMode(Component component)
		{
			Material? material = GetMaterial(component);
			if (material != null)
			{
				return (ColorMode)(int)material.GetFloat(Material.ColorModePropertyName, 3.0f);
			}
			return (ColorMode)NativeApi.VspComponent_GetColorMode(component.NativeHandle);
		}

		/// <summary>Whether this component contributes a triangle to the frame.</summary>
		public static bool IsRenderable(Component component) =>
			NativeApi.VspComponent_IsRenderable(component.NativeHandle) != 0;

		/// <summary>Makes the component take part in (or drop out of) the frame.</summary>
		public static void SetRenderable(Component component, bool isRenderable) =>
			NativeApi.VspComponent_SetRenderable(component.NativeHandle, isRenderable ? 1 : 0);
	}
}
