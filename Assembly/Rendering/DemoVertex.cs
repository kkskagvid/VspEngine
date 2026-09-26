using System.Numerics;
using System.Runtime.InteropServices;

namespace Assembly.Rendering
{
	/// <summary>
	/// One vertex of the demo meshes: position, normal and vertex colour.
	///
	/// The struct is blittable and the layout the pipeline declares to the
	/// backend must match it exactly (stride 40: float3 at 0, float3 at 12,
	/// float4 at 24), which is also the order the shader's VSP_VK_LOCATION
	/// attributes name.
	///
	/// There is no texture coordinate on purpose: the demo's lit cube and its
	/// ground plate are not textured, and a vertex attribute a shader does not
	/// consume is exactly what the Vulkan validation layer reports as a
	/// pipeline/shader mismatch. A game that textures its meshes adds the field
	/// and the attribute together.
	///
	/// The vertex format belongs to the game: the engine only knows how to
	/// allocate a buffer and how to describe a layout to the graphics backend.
	/// </summary>
	[StructLayout(LayoutKind.Sequential, Pack = 4)]
	public struct DemoVertex
	{
		public Vector3 Position;
		public Vector3 Normal;
		public Vector4 Color;

		public DemoVertex(Vector3 position, Vector3 normal, Vector4 color)
		{
			Position = position;
			Normal = normal;
			Color = color;
		}

		/// <summary>Bytes one vertex occupies in a vertex buffer.</summary>
		public const uint Stride = 40;
	}
}
