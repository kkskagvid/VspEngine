namespace VspEngine
{
	/// <summary>
	/// Per-object render state the engine keeps for every pipeline: the material
	/// a component draws with and whether it takes part in the frame.
	///
	/// The values live in the native scene (on the component), so a render
	/// pipeline can read them while it builds a frame without calling back into
	/// script code. Anything a particular shader needs beyond that is a property
	/// of the material - the engine names no shader property of its own.
	/// </summary>
	public static class Renderer
	{
		/// <summary>
		/// The material this component draws with. A pipeline assigns one to every
		/// renderable component, so this returns null only before that happened.
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

		/// <summary>Whether this component takes part in the frame.</summary>
		public static bool IsRenderable(Component component) =>
			NativeApi.VspComponent_IsRenderable(component.NativeHandle) != 0;

		/// <summary>Makes the component take part in (or drop out of) the frame.</summary>
		public static void SetRenderable(Component component, bool isRenderable) =>
			NativeApi.VspComponent_SetRenderable(component.NativeHandle, isRenderable ? 1 : 0);
	}
}
