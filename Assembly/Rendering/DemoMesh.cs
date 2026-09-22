using System.Numerics;

namespace Assembly.Rendering
{
	/// <summary>
	/// The meshes the demo draws: a colored triangle (the demo's "2D" content,
	/// placed in 3D like everything else) and a cube, whose twelve triangles are
	/// drawn from eight shared corners through an index buffer.
	///
	/// A mesh here is just the two arrays a draw needs; the engine never sees a
	/// mesh type, only buffers.
	/// </summary>
	public sealed class DemoMesh
	{
		/// <summary>Vertices of the mesh, in the layout <see cref="DemoVertex"/> describes.</summary>
		public DemoVertex[] Vertices { get; }

		/// <summary>Indices into <see cref="Vertices"/>, three per triangle.</summary>
		public uint[] Indices { get; }

		public DemoMesh(DemoVertex[] vertices, uint[] indices)
		{
			Vertices = vertices;
			Indices = indices;
		}

		public int TriangleCount => Indices.Length / 3;

		/// <summary>
		/// The demo triangle: three corners with distinct vertex colors, facing
		/// +Z. Its vertices carry their own colors, which is what the multicolor
		/// mode shows.
		/// </summary>
		public static DemoMesh CreateColoredTriangle()
		{
			DemoVertex[] vertices =
			{
				// bottom-left (red)     bottom-right (green)   top (blue)
				new DemoVertex(new Vector3(-0.6f, -0.5f, 0.0f), new Vector3(0, 0, 1), new Vector4(1, 0, 0, 1), new Vector2(0.0f, 0.0f)),
				new DemoVertex(new Vector3( 0.6f, -0.5f, 0.0f), new Vector3(0, 0, 1), new Vector4(0, 1, 0, 1), new Vector2(1.0f, 0.0f)),
				new DemoVertex(new Vector3( 0.0f,  0.6f, 0.0f), new Vector3(0, 0, 1), new Vector4(0, 0, 1, 1), new Vector2(0.5f, 1.0f)),
			};

			return new DemoMesh(vertices, new uint[] { 0, 1, 2 });
		}

		/// <summary>
		/// A unit cube centred on the origin, one corner color per face. Each
		/// face keeps its own four corners (and its own normal), so the shape
		/// reads as a solid instead of one smooth blob.
		/// </summary>
		public static DemoMesh CreateUnitCube()
		{
			DemoVertex[] vertices =
			{
				// +Z (front)                                   -Z (back)
				Corner(-0.5f, -0.5f,  0.5f, 0, 0, 1, 0.8f, 0.8f, 0.9f),
				Corner( 0.5f, -0.5f,  0.5f, 0, 0, 1, 0.8f, 0.8f, 0.9f),
				Corner( 0.5f,  0.5f,  0.5f, 0, 0, 1, 0.8f, 0.8f, 0.9f),
				Corner(-0.5f,  0.5f,  0.5f, 0, 0, 1, 0.8f, 0.8f, 0.9f),

				Corner(-0.5f, -0.5f, -0.5f, 0, 0, -1, 0.6f, 0.6f, 0.7f),
				Corner(-0.5f,  0.5f, -0.5f, 0, 0, -1, 0.6f, 0.6f, 0.7f),
				Corner( 0.5f,  0.5f, -0.5f, 0, 0, -1, 0.6f, 0.6f, 0.7f),
				Corner( 0.5f, -0.5f, -0.5f, 0, 0, -1, 0.6f, 0.6f, 0.7f),

				// +X (right)                                   -X (left)
				Corner( 0.5f, -0.5f,  0.5f, 1, 0, 0, 0.7f, 0.7f, 0.8f),
				Corner( 0.5f, -0.5f, -0.5f, 1, 0, 0, 0.7f, 0.7f, 0.8f),
				Corner( 0.5f,  0.5f, -0.5f, 1, 0, 0, 0.7f, 0.7f, 0.8f),
				Corner( 0.5f,  0.5f,  0.5f, 1, 0, 0, 0.7f, 0.7f, 0.8f),

				Corner(-0.5f, -0.5f, -0.5f, -1, 0, 0, 0.5f, 0.5f, 0.6f),
				Corner(-0.5f, -0.5f,  0.5f, -1, 0, 0, 0.5f, 0.5f, 0.6f),
				Corner(-0.5f,  0.5f,  0.5f, -1, 0, 0, 0.5f, 0.5f, 0.6f),
				Corner(-0.5f,  0.5f, -0.5f, -1, 0, 0, 0.5f, 0.5f, 0.6f),

				// +Y (top)                                     -Y (bottom)
				Corner(-0.5f,  0.5f,  0.5f, 0, 1, 0, 0.9f, 0.9f, 1.0f),
				Corner( 0.5f,  0.5f,  0.5f, 0, 1, 0, 0.9f, 0.9f, 1.0f),
				Corner( 0.5f,  0.5f, -0.5f, 0, 1, 0, 0.9f, 0.9f, 1.0f),
				Corner(-0.5f,  0.5f, -0.5f, 0, 1, 0, 0.9f, 0.9f, 1.0f),

				Corner(-0.5f, -0.5f, -0.5f, 0, -1, 0, 0.4f, 0.4f, 0.5f),
				Corner( 0.5f, -0.5f, -0.5f, 0, -1, 0, 0.4f, 0.4f, 0.5f),
				Corner( 0.5f, -0.5f,  0.5f, 0, -1, 0, 0.4f, 0.4f, 0.5f),
				Corner(-0.5f, -0.5f,  0.5f, 0, -1, 0, 0.4f, 0.4f, 0.5f),
			};

			// Two triangles per face, wound counter-clockwise when seen from
			// outside, which is the winding the pipeline's back-face culling
			// expects.
			uint[] indices = new uint[36];
			for (uint faceIndex = 0; faceIndex < 6; ++faceIndex)
			{
				uint corner = faceIndex * 4;
				uint index = faceIndex * 6;
				indices[index + 0] = corner + 0;
				indices[index + 1] = corner + 1;
				indices[index + 2] = corner + 2;
				indices[index + 3] = corner + 0;
				indices[index + 4] = corner + 2;
				indices[index + 5] = corner + 3;
			}

			return new DemoMesh(vertices, indices);
		}

		private static DemoVertex Corner(
			float x, float y, float z,
			float normalX, float normalY, float normalZ,
			float r, float g, float b)
		{
			return new DemoVertex(
				new Vector3(x, y, z), new Vector3(normalX, normalY, normalZ), new Vector4(r, g, b, 1.0f), new Vector2(0.0f, 0.0f));
		}
	}
}
