using System.Numerics;
using System.Runtime.InteropServices;

namespace Assembly.Rendering
{
	/// <summary>
	/// One vertex of the demo triangle: position, RGBA color and texture
	/// coordinate. The struct is blittable and the layout the pipeline declares
	/// to the backend must match it exactly (stride 32, float2 at 0, float4 at 8,
	/// float2 at 24).
	///
	/// The vertex format belongs to the game: the engine only knows how to
	/// allocate a buffer and how to describe a layout to the graphics backend.
	/// </summary>
	[StructLayout(LayoutKind.Sequential, Pack = 4)]
	public struct TriangleVertex
	{
		public Vector2 Position;
		public Vector4 Color;
		public Vector2 Uv;

		public TriangleVertex(Vector2 position, Vector4 color, Vector2 uv)
		{
			Position = position;
			Color = color;
			Uv = uv;
		}

		/// <summary>Bytes one vertex occupies in a vertex buffer.</summary>
		public const uint Stride = 32;
	}
}
