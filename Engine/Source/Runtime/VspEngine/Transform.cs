using System;
using System.Numerics;

namespace VspEngine
{
	/// <summary>
	/// Reference handle for a native transform, and the managed view of the two
	/// coordinate spaces an object lives in:
	///
	///   LOCAL  where the object sits relative to its parent: the values a script
	///          writes, and the ones a scene file stores.
	///   WORLD  where it ends up in the scene after the whole parent chain has
	///          been applied: the values the camera and the renderer read.
	///
	/// Both live in the native scene graph (the local values as stored data, the
	/// world matrix derived from the parent chain and cached there), so a
	/// transform shared between scripts always reads the same numbers, and
	/// <see cref="SceneSerializer"/> records a snapshot of the two.
	///
	/// Matrices are the engine's convention: column-major 4x4, right-handed, Y up,
	/// an object's forward being its local -Z. <see cref="Matrix4x4"/> stores rows,
	/// so the getters transpose what the native side returns.
	/// </summary>
	public sealed class Transform : Object
	{
		// Scratch buffers the native getters fill; kept per instance so reading a
		// transform allocates nothing.
		private readonly float[] xyzScratch = new float[3];
		private readonly float[] matrixScratch = new float[16];

		internal Transform(uint nativeHandle)
		{
			NativeHandle = nativeHandle;
		}

		/// <summary>The game object this transform belongs to (0 when detached).</summary>
		public uint OwnerGameObjectHandle => NativeApi.VspTransform_GetOwnerGameObject(NativeHandle);

		// -----------------------------------------------------------------
		// Local coordinates: where the object sits relative to its parent.
		// -----------------------------------------------------------------

		/// <summary>Position relative to the parent (world units).</summary>
		public Vector3 LocalPosition
		{
			get
			{
				NativeApi.VspTransform_GetLocalPosition(NativeHandle, xyzScratch);
				return new Vector3(xyzScratch[0], xyzScratch[1], xyzScratch[2]);
			}
			set => NativeApi.VspTransform_SetLocalPosition(NativeHandle, value.X, value.Y, value.Z);
		}

		/// <summary>Rotation relative to the parent, in Euler angles (degrees).</summary>
		public Vector3 LocalEulerAngles
		{
			get
			{
				NativeApi.VspTransform_GetLocalRotation(NativeHandle, xyzScratch);
				return new Vector3(xyzScratch[0], xyzScratch[1], xyzScratch[2]);
			}
			set => NativeApi.VspTransform_SetLocalRotation(NativeHandle, value.X, value.Y, value.Z);
		}

		/// <summary>Scale relative to the parent (1, 1, 1 by default).</summary>
		public Vector3 LocalScale
		{
			get
			{
				NativeApi.VspTransform_GetLocalScale(NativeHandle, xyzScratch);
				return new Vector3(xyzScratch[0], xyzScratch[1], xyzScratch[2]);
			}
			set => NativeApi.VspTransform_SetLocalScale(NativeHandle, value.X, value.Y, value.Z);
		}

		/// <summary>The local transform as a matrix (translation * rotation * scale).</summary>
		public Matrix4x4 LocalToParentMatrix
		{
			get
			{
				NativeApi.VspTransform_GetLocalMatrix(NativeHandle, matrixScratch);
				return ToMatrix4x4(matrixScratch);
			}
		}

		// -----------------------------------------------------------------
		// World coordinates: where the object ends up in the scene.
		// -----------------------------------------------------------------

		/// <summary>Position in the scene, with the whole parent chain applied.</summary>
		public Vector3 Position
		{
			get
			{
				NativeApi.VspTransform_GetWorldPosition(NativeHandle, xyzScratch);
				return new Vector3(xyzScratch[0], xyzScratch[1], xyzScratch[2]);
			}
			set => NativeApi.VspTransform_SetWorldPosition(NativeHandle, value.X, value.Y, value.Z);
		}

		/// <summary>World rotation in Euler angles (degrees).</summary>
		public Vector3 EulerAngles
		{
			get
			{
				NativeApi.VspTransform_GetWorldRotation(NativeHandle, xyzScratch);
				return new Vector3(xyzScratch[0], xyzScratch[1], xyzScratch[2]);
			}
		}

		/// <summary>World scale: the local scale with every parent's scale applied.</summary>
		public Vector3 LossyScale
		{
			get
			{
				NativeApi.VspTransform_GetWorldScale(NativeHandle, xyzScratch);
				return new Vector3(xyzScratch[0], xyzScratch[1], xyzScratch[2]);
			}
		}

		/// <summary>Takes a point from this object's local space into the scene.</summary>
		public Matrix4x4 LocalToWorldMatrix
		{
			get
			{
				NativeApi.VspTransform_GetWorldMatrix(NativeHandle, matrixScratch);
				return ToMatrix4x4(matrixScratch);
			}
		}

		/// <summary>Takes a point from the scene into this object's local space.</summary>
		public Matrix4x4 WorldToLocalMatrix
		{
			get
			{
				NativeApi.VspTransform_GetWorldToLocalMatrix(NativeHandle, matrixScratch);
				return ToMatrix4x4(matrixScratch);
			}
		}

		/// <summary>Takes a world point into this object's local space.</summary>
		public Vector3 InverseTransformPoint(Vector3 worldPosition) =>
			Vector3.Transform(worldPosition, WorldToLocalMatrix);

		/// <summary>Takes a local point into the scene.</summary>
		public Vector3 TransformPoint(Vector3 localPosition) =>
			Vector3.Transform(localPosition, LocalToWorldMatrix);

		/// <summary>A world direction taken into this object's local space.</summary>
		public Vector3 InverseTransformDirection(Vector3 worldDirection) =>
			Vector3.Normalize(Vector3.TransformNormal(worldDirection, WorldToLocalMatrix));

		/// <summary>A local direction taken into the scene.</summary>
		public Vector3 TransformDirection(Vector3 localDirection) =>
			Vector3.Normalize(Vector3.TransformNormal(localDirection, LocalToWorldMatrix));

		/// <summary>Where this object looks, in world space (its local -Z).</summary>
		public Vector3 Forward => TransformDirection(new Vector3(0.0f, 0.0f, -1.0f));

		/// <summary>Where this object's up points, in world space (its local +Y).</summary>
		public Vector3 Up => TransformDirection(new Vector3(0.0f, 1.0f, 0.0f));

		/// <summary>Where this object's right points, in world space (its local +X).</summary>
		public Vector3 Right => TransformDirection(new Vector3(1.0f, 0.0f, 0.0f));

		// -----------------------------------------------------------------
		// Hierarchy
		// -----------------------------------------------------------------

		/// <summary>The parent transform, or null when this transform is a scene root.</summary>
		public Transform? Parent
		{
			get
			{
				uint parentHandle = NativeApi.VspTransform_GetParent(NativeHandle);
				return parentHandle != 0 ? new Transform(parentHandle) : null;
			}
		}

		/// <summary>Number of transforms parented directly to this one.</summary>
		public int ChildCount => NativeApi.VspTransform_GetChildCount(NativeHandle);

		/// <summary>The child at the given index.</summary>
		public Transform? GetChild(int childIndex)
		{
			if (childIndex < 0 || childIndex >= ChildCount)
			{
				return null;
			}
			uint childHandle = NativeApi.VspTransform_GetChild(NativeHandle, childIndex);
			return childHandle != 0 ? new Transform(childHandle) : null;
		}

		/// <summary>
		/// Parents this transform to another one, keeping its local coordinates
		/// (so the object moves with its new parent).
		/// </summary>
		public bool SetParent(Transform? parent) => SetParent(parent, false);

		/// <summary>
		/// Parents this transform to another one (null detaches it to the scene
		/// root). With keepWorldPosition the object stays where it is on screen
		/// and only its local coordinates change; without it the local coordinates
		/// stay and the object moves with its new parent.
		/// Returns false when the parent would create a cycle.
		/// </summary>
		public bool SetParent(Transform? parent, bool keepWorldPosition)
		{
			Vector3 worldPosition = keepWorldPosition ? Position : Vector3.Zero;

			uint parentHandle = parent?.NativeHandle ?? 0;
			if (NativeApi.VspTransform_SetParent(NativeHandle, parentHandle) == 0)
			{
				return false;
			}

			if (keepWorldPosition)
			{
				Position = worldPosition;
			}
			return true;
		}

		// -----------------------------------------------------------------
		// Change tracking
		// -----------------------------------------------------------------

		/// <summary>True while the transform changed since the flag was cleared.</summary>
		public bool IsDirty => NativeApi.VspTransform_IsDirty(NativeHandle) != 0;

		/// <summary>Clears the change flag (a system that consumed the value does this).</summary>
		public void ClearDirtyFlag() => NativeApi.VspTransform_ClearDirtyFlag(NativeHandle);

		/// <summary>
		/// Turns the native column-major array into a <see cref="Matrix4x4"/>. The
		/// native layout stores element (row, column) at column * 4 + row, while
		/// the Matrix4x4 constructor reads one ROW per four values, so the array is
		/// transposed on the way in.
		/// </summary>
		private static Matrix4x4 ToMatrix4x4(float[] columnMajorValues)
		{
			return new Matrix4x4(
				columnMajorValues[0], columnMajorValues[4], columnMajorValues[8], columnMajorValues[12],
				columnMajorValues[1], columnMajorValues[5], columnMajorValues[9], columnMajorValues[13],
				columnMajorValues[2], columnMajorValues[6], columnMajorValues[10], columnMajorValues[14],
				columnMajorValues[3], columnMajorValues[7], columnMajorValues[11], columnMajorValues[15]);
		}
	}
}
