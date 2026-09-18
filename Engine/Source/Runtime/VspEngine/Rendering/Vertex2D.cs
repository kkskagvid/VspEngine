using System.Numerics;
using System.Runtime.InteropServices;

namespace VspEngine.Rendering
{
	/// <summary>
	/// The vertex layout the engine's built-in 2D pipeline uses: position, RGBA
	/// color and texture coordinate. The struct is blittable and matches the
	/// attribute descriptions the pipeline builder publishes (stride 32, float2
	/// at 0, float4 at 8, float2 at 24).
	/// </summary>
	[StructLayout(LayoutKind.Sequential, Pack = 4)]
	public struct Vertex2D
	{
		public Vector2 Position;
		public Vector4 Color;
		public Vector2 Uv;

		public Vertex2D(Vector2 position, Vector4 color, Vector2 uv)
		{
			Position = position;
			Color = color;
			Uv = uv;
		}

		/// <summary>Number of bytes one vertex occupies in a vertex buffer.</summary>
		public const uint Stride = 32;
	}
}
