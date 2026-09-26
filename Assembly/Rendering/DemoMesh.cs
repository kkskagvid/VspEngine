using System.Numerics;

namespace Assembly.Rendering
{
	/// <summary>
	/// The meshes the demo draws.
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
		/// The demo's unit cube: centred on the origin, one SATURATED colour per
		/// face so the shape reads as a solid and a spin about Y is unmistakable.
		///
		/// Each face keeps its own four corners and its own normal, so flat
		/// shading gives every side a single tone instead of smearing one corner
		/// colour into the next.
		/// </summary>
		public static DemoMesh CreateUnitCube()
		{
			DemoVertex[] vertices =
			{
				// +Z (front) - red                                    -Z (back) - cyan
				Corner(-0.5f, -0.5f,  0.5f, 0, 0, 1, 0.95f, 0.26f, 0.21f),
				Corner( 0.5f, -0.5f,  0.5f, 0, 0, 1, 0.95f, 0.26f, 0.21f),
				Corner( 0.5f,  0.5f,  0.5f, 0, 0, 1, 0.95f, 0.26f, 0.21f),
				Corner(-0.5f,  0.5f,  0.5f, 0, 0, 1, 0.95f, 0.26f, 0.21f),

				Corner(-0.5f, -0.5f, -0.5f, 0, 0, -1, 0.13f, 0.83f, 0.82f),
				Corner(-0.5f,  0.5f, -0.5f, 0, 0, -1, 0.13f, 0.83f, 0.82f),
				Corner( 0.5f,  0.5f, -0.5f, 0, 0, -1, 0.13f, 0.83f, 0.82f),
				Corner( 0.5f, -0.5f, -0.5f, 0, 0, -1, 0.13f, 0.83f, 0.82f),

				// +X (right) - green                                  -X (left) - magenta
				Corner( 0.5f, -0.5f,  0.5f, 1, 0, 0, 0.30f, 0.85f, 0.39f),
				Corner( 0.5f, -0.5f, -0.5f, 1, 0, 0, 0.30f, 0.85f, 0.39f),
				Corner( 0.5f,  0.5f, -0.5f, 1, 0, 0, 0.30f, 0.85f, 0.39f),
				Corner( 0.5f,  0.5f,  0.5f, 1, 0, 0, 0.30f, 0.85f, 0.39f),

				Corner(-0.5f, -0.5f, -0.5f, -1, 0, 0, 0.85f, 0.30f, 0.75f),
				Corner(-0.5f, -0.5f,  0.5f, -1, 0, 0, 0.85f, 0.30f, 0.75f),
				Corner(-0.5f,  0.5f,  0.5f, -1, 0, 0, 0.85f, 0.30f, 0.75f),
				Corner(-0.5f,  0.5f, -0.5f, -1, 0, 0, 0.85f, 0.30f, 0.75f),

				// +Y (top) - yellow (the face the overhead light lands on)
				// -Y (bottom) - blue
				Corner(-0.5f,  0.5f,  0.5f, 0, 1, 0, 0.98f, 0.82f, 0.25f),
				Corner( 0.5f,  0.5f,  0.5f, 0, 1, 0, 0.98f, 0.82f, 0.25f),
				Corner( 0.5f,  0.5f, -0.5f, 0, 1, 0, 0.98f, 0.82f, 0.25f),
				Corner(-0.5f,  0.5f, -0.5f, 0, 1, 0, 0.98f, 0.82f, 0.25f),

				Corner(-0.5f, -0.5f, -0.5f, 0, -1, 0, 0.25f, 0.45f, 0.95f),
				Corner( 0.5f, -0.5f, -0.5f, 0, -1, 0, 0.25f, 0.45f, 0.95f),
				Corner( 0.5f, -0.5f,  0.5f, 0, -1, 0, 0.25f, 0.45f, 0.95f),
				Corner(-0.5f, -0.5f,  0.5f, 0, -1, 0, 0.25f, 0.45f, 0.95f),
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

		/// <summary>
		/// A flat square on the XZ plane with its normal pointing up. The demo
		/// hangs it under the cube as a ground plate, which is what makes the
		/// movement and the camera orbit legible: without a fixed reference the
		/// cube would look stationary while the world slid past.
		/// </summary>
		public static DemoMesh CreateGroundPlane(float halfExtent)
		{
			Vector4 groundColor = new Vector4(0.30f, 0.33f, 0.38f, 1.0f);
			DemoVertex[] vertices =
			{
				new DemoVertex(new Vector3(-halfExtent, 0.0f, -halfExtent), new Vector3(0, 1, 0), groundColor),
				new DemoVertex(new Vector3(-halfExtent, 0.0f,  halfExtent), new Vector3(0, 1, 0), groundColor),
				new DemoVertex(new Vector3( halfExtent, 0.0f,  halfExtent), new Vector3(0, 1, 0), groundColor),
				new DemoVertex(new Vector3( halfExtent, 0.0f, -halfExtent), new Vector3(0, 1, 0), groundColor),
			};

			return new DemoMesh(vertices, new uint[] { 0, 1, 2, 0, 2, 3 });
		}

		private static DemoVertex Corner(
			float x, float y, float z,
			float normalX, float normalY, float normalZ,
			float r, float g, float b)
		{
			return new DemoVertex(
				new Vector3(x, y, z), new Vector3(normalX, normalY, normalZ), new Vector4(r, g, b, 1.0f));
		}
	}
}
