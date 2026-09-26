using System.Numerics;

using VspEngine;

namespace Assembly.Rendering
{
	/// <summary>
	/// The shader properties the demo drives, and the small helpers that read and
	/// write them on a component's material.
	///
	/// The names live here, in the game, because they are written in the game's
	/// shader (Assembly/Shaders/LitCube.vsf): the engine neither knows nor cares
	/// that "_BaseColor" exists.
	/// </summary>
	public static class LitCubeMaterial
	{
		/// <summary>Property that multiplies every vertex colour.</summary>
		public const string BaseColorPropertyName = "_BaseColor";

		/// <summary>Property that scales the ambient fill the sides receive.</summary>
		public const string AmbientPropertyName = "_Ambient";

		/// <summary>
		/// Sets the colour a component's material multiplies its vertex colours
		/// with. A component that has no material yet keeps the one the pipeline
		/// hands it on the first frame, so this returns false until then.
		/// </summary>
		public static bool SetBaseColor(Component component, Vector4 color)
		{
			Material? material = Renderer.GetMaterial(component);
			return material != null && material.SetVector(BaseColorPropertyName, color);
		}

		/// <summary>Sets how much ambient fill the material's shading adds.</summary>
		public static bool SetAmbient(Component component, float ambient)
		{
			Material? material = Renderer.GetMaterial(component);
			return material != null && material.SetFloat(AmbientPropertyName, ambient);
		}
	}
}
