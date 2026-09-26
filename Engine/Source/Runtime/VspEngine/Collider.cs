using System;
using System.Collections.Generic;
using System.Numerics;

namespace VspEngine
{
	/// <summary>The volume a collider occupies.</summary>
	public enum ColliderShape
	{
		/// <summary>A solid cuboid: the shape that fits a crate, a wall or a cube.</summary>
		Box = 0,

		/// <summary>A ball of one radius: the cheapest shape to test.</summary>
		Sphere = 1,

		/// <summary>A triangle soup: the shape of real level geometry.</summary>
		Mesh = 2,
	}

	/// <summary>
	/// Reference handle for a native collider: the SHAPE the physics simulation
	/// tests against everything else, placed by the transform of the game object
	/// it belongs to.
	///
	/// A shape is described in the collider's own local space, so moving or
	/// turning the object moves the shape with it. What a shape is USED for
	/// depends on the object: with a <see cref="Rigidbody"/> the object moves and
	/// the shape travels with it, without one the object is level geometry that
	/// every body collides with.
	///
	/// The two surface values - <see cref="Restitution"/> and
	/// <see cref="Friction"/> - are what a contact between two colliders is
	/// resolved with: how much of the approach speed comes back, and how strongly
	/// sliding along the surface is resisted.
	///
	/// Instances are created through the shape's own class
	/// (<see cref="BoxCollider"/>, <see cref="SphereCollider"/>,
	/// <see cref="MeshCollider"/>); this base type is what a scene walks and what
	/// a query hands back.
	/// </summary>
	public class Collider : Object
	{
		private readonly float[] xyzScratch = new float[3];
		private readonly float[] minimumScratch = new float[3];
		private readonly float[] maximumScratch = new float[3];

		internal Collider(uint nativeHandle)
		{
			NativeHandle = nativeHandle;
		}

		/// <summary>
		/// Creates the native collider of the given shape on a game object, or
		/// null when the object is gone.
		/// </summary>
		internal static Collider? CreateShape(GameObject gameObject, ColliderShape shape)
		{
			if (gameObject == null)
			{
				throw new ArgumentNullException(nameof(gameObject));
			}

			uint nativeHandle = NativeApi.VspCollider_Create(gameObject.NativeHandle);
			if (nativeHandle == 0)
			{
				return null;
			}

			NativeApi.VspCollider_SetShape(nativeHandle, (int)shape);
			return new Collider(nativeHandle);
		}

		/// <summary>Every collider of the scene, in the order the scene stores them.</summary>
		public static Collider[] All()
		{
			int colliderCount = (int)NativeApi.VspScene_GetLiveColliderCount();
			List<Collider> colliders = new List<Collider>(colliderCount);
			for (uint colliderIndex = 0; colliderIndex < colliderCount; ++colliderIndex)
			{
				uint colliderHandle = NativeApi.VspScene_GetColliderHandle(colliderIndex);
				if (colliderHandle != 0)
				{
					colliders.Add(new Collider(colliderHandle));
				}
			}
			return colliders.ToArray();
		}

		/// <summary>The colliders attached to one game object.</summary>
		public static Collider[] GetColliders(GameObject gameObject)
		{
			if (gameObject == null)
			{
				throw new ArgumentNullException(nameof(gameObject));
			}

			List<Collider> colliders = new List<Collider>();
			foreach (Collider collider in All())
			{
				if (NativeApi.VspCollider_GetGameObject(collider.NativeHandle) == gameObject.NativeHandle)
				{
					colliders.Add(collider);
				}
			}
			return colliders.ToArray();
		}

		/// <summary>The game object whose transform places this shape.</summary>
		public GameObject? OwnerGameObject
		{
			get
			{
				uint gameObjectHandle = NativeApi.VspCollider_GetGameObject(NativeHandle);
				return gameObjectHandle != 0 ? new GameObject(gameObjectHandle) : null;
			}
		}

		/// <summary>Which volume this collider occupies.</summary>
		public ColliderShape Shape
		{
			get => (ColliderShape)NativeApi.VspCollider_GetShape(NativeHandle);
			set => NativeApi.VspCollider_SetShape(NativeHandle, (int)value);
		}

		// -----------------------------------------------------------------
		// Placement
		// -----------------------------------------------------------------

		/// <summary>
		/// Offset of the shape's centre from the object's origin, in the object's
		/// own space. It is what lets one object carry a shape that is not centred
		/// on it - a foot collider under a character, for instance.
		/// </summary>
		public Vector3 Center
		{
			get
			{
				NativeApi.VspCollider_GetCenter(NativeHandle, xyzScratch);
				return new Vector3(xyzScratch[0], xyzScratch[1], xyzScratch[2]);
			}
			set => NativeApi.VspCollider_SetCenter(NativeHandle, value.X, value.Y, value.Z);
		}

		// -----------------------------------------------------------------
		// Surface
		// -----------------------------------------------------------------

		/// <summary>
		/// How much of the approach speed a contact gives back: 0 stops the body
		/// dead, 1 sends it back the way it came. Two colliders combine as the
		/// bouncier of the two.
		/// </summary>
		public float Restitution
		{
			get => NativeApi.VspCollider_GetRestitution(NativeHandle);
			set => NativeApi.VspCollider_SetRestitution(NativeHandle, value);
		}

		/// <summary>
		/// How strongly a contact resists sliding along the surface: 0 is ice, 1
		/// stops the tangential motion completely. Two colliders combine as the
		/// geometric mean of their values.
		/// </summary>
		public float Friction
		{
			get => NativeApi.VspCollider_GetFriction(NativeHandle);
			set => NativeApi.VspCollider_SetFriction(NativeHandle, value);
		}

		// -----------------------------------------------------------------
		// Where the shape is in the world
		// -----------------------------------------------------------------

		/// <summary>Centre of the shape in the scene, in world units.</summary>
		public Vector3 WorldCenter
		{
			get
			{
				NativeApi.VspCollider_GetWorldCenter(NativeHandle, xyzScratch);
				return new Vector3(xyzScratch[0], xyzScratch[1], xyzScratch[2]);
			}
		}

		/// <summary>
		/// World-space bounding box of the shape. The size of it says how big the
		/// shape is in the scene, which is what a broadphase compares.
		/// </summary>
		public void GetWorldBounds(out Vector3 minimum, out Vector3 maximum)
		{
			NativeApi.VspCollider_GetWorldBounds(NativeHandle, minimumScratch, maximumScratch);
			minimum = new Vector3(minimumScratch[0], minimumScratch[1], minimumScratch[2]);
			maximum = new Vector3(maximumScratch[0], maximumScratch[1], maximumScratch[2]);
		}

		/// <summary>Releases the native collider; the object it belonged to stays.</summary>
		public void Destroy()
		{
			NativeApi.VspCollider_Destroy(NativeHandle);
			NativeHandle = 0;
		}

		/// <summary>Reads a three-float native value into a vector.</summary>
		internal static Vector3 ReadVector(float[] values) => new Vector3(values[0], values[1], values[2]);
	}

	/// <summary>
	/// A box-shaped collider. <see cref="Size"/> is the full size of the box
	/// along each of the object's own axes, so a size of (1, 1, 1) on an object
	/// scaled to 1 fits a unit cube exactly.
	/// </summary>
	public sealed class BoxCollider : Collider
	{
		private readonly float[] halfExtentsScratch = new float[3];

		internal BoxCollider(uint nativeHandle)
			: base(nativeHandle)
		{
		}

		/// <summary>Gives a game object a box-shaped collider.</summary>
		public static BoxCollider? Create(GameObject gameObject)
		{
			Collider? collider = CreateShape(gameObject, ColliderShape.Box);
			return collider != null ? new BoxCollider(collider.NativeHandle) : null;
		}

		/// <summary>Full size of the box along each local axis.</summary>
		public Vector3 Size
		{
			get => HalfExtents * 2.0f;
			set => HalfExtents = value * 0.5f;
		}

		/// <summary>
		/// Half the size of the box along each local axis - the form the shape is
		/// stored in, because that is the form the collision tests use.
		/// </summary>
		public Vector3 HalfExtents
		{
			get
			{
				NativeApi.VspCollider_GetBoxHalfExtents(NativeHandle, halfExtentsScratch);
				return ReadVector(halfExtentsScratch);
			}
			set => NativeApi.VspCollider_SetBoxHalfExtents(NativeHandle, value.X, value.Y, value.Z);
		}
	}

	/// <summary>A ball-shaped collider: the cheapest shape to test, and the one a character or a projectile is approximated with.</summary>
	public sealed class SphereCollider : Collider
	{
		internal SphereCollider(uint nativeHandle)
			: base(nativeHandle)
		{
		}

		/// <summary>Gives a game object a ball-shaped collider.</summary>
		public static SphereCollider? Create(GameObject gameObject)
		{
			Collider? collider = CreateShape(gameObject, ColliderShape.Sphere);
			return collider != null ? new SphereCollider(collider.NativeHandle) : null;
		}

		/// <summary>Radius of the ball, in the object's own units.</summary>
		public float Radius
		{
			get => NativeApi.VspCollider_GetSphereRadius(NativeHandle);
			set => NativeApi.VspCollider_SetSphereRadius(NativeHandle, value);
		}
	}

	/// <summary>
	/// A triangle-soup collider: the shape of real level geometry, which is never
	/// a box.
	///
	/// The mesh is uploaded once, in the object's own local space, and placed in
	/// the world by the object's transform every step. A mesh collider is meant
	/// to be level geometry: the simulation tests other shapes against its
	/// triangles, and does not answer mesh-against-mesh.
	/// </summary>
	public sealed class MeshCollider : Collider
	{
		internal MeshCollider(uint nativeHandle)
			: base(nativeHandle)
		{
		}

		/// <summary>Gives a game object a mesh-shaped collider (with no mesh yet).</summary>
		public static MeshCollider? Create(GameObject gameObject)
		{
			Collider? collider = CreateShape(gameObject, ColliderShape.Mesh);
			return collider != null ? new MeshCollider(collider.NativeHandle) : null;
		}

		/// <summary>Vertices of the mesh, in the object's local space.</summary>
		public int VertexCount => (int)NativeApi.VspCollider_GetMeshVertexCount(NativeHandle);

		/// <summary>Triangles of the mesh.</summary>
		public int TriangleCount => (int)NativeApi.VspCollider_GetMeshTriangleCount(NativeHandle);

		/// <summary>
		/// Replaces the mesh. The indices are three per triangle and address the
		/// vertex array, so a mesh built for rendering can be handed over as it
		/// stands. Returns false - leaving the previous mesh in place - when the
		/// data is unusable (no vertices, an index count that is not a multiple of
		/// three, an index that addresses no vertex).
		/// </summary>
		public bool SetMesh(Vector3[] vertices, uint[] indices)
		{
			if (vertices == null)
			{
				throw new ArgumentNullException(nameof(vertices));
			}
			if (indices == null)
			{
				throw new ArgumentNullException(nameof(indices));
			}

			// The native side takes the positions as one flat float array, which is
			// the layout a vertex buffer has anyway.
			float[] positionsXyz = new float[vertices.Length * 3];
			for (int vertexIndex = 0; vertexIndex < vertices.Length; ++vertexIndex)
			{
				Vector3 vertex = vertices[vertexIndex];
				positionsXyz[(vertexIndex * 3) + 0] = vertex.X;
				positionsXyz[(vertexIndex * 3) + 1] = vertex.Y;
				positionsXyz[(vertexIndex * 3) + 2] = vertex.Z;
			}

			return NativeApi.VspCollider_SetMesh(
				NativeHandle, positionsXyz, (uint)vertices.Length, indices, (uint)indices.Length) != 0;
		}
	}
}
