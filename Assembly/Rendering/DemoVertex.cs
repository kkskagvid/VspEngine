using System.Numerics;
using System.Runtime.InteropServices;

namespace Assembly.Rendering
{
	/// <summary>
	/// One vertex of the demo meshes: position, normal, vertex color and texture
	/// coordinate.
	///
	/// The struct is blittable and the layout the pipeline declares to the
	/// backend must match it exactly (stride 48: float3 at 0, float3 at 12,
	/// float4 at 24, float2 at 40), which is also the order the shader's
	/// VSP_VK_LOCATION attributes name.
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
		public Vector2 Uv;

		public DemoVertex(Vector3 position, Vector3 normal, Vector4 color, Vector2 uv)
		{
			Position = position;
			Normal = normal;
			Color = color;
			Uv = uv;
		}

		/// <summary>Bytes one vertex occupies in a vertex buffer.</summary>
		public const uint Stride = 48;
	}
}
