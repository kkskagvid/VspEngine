using System;
using System.Numerics;

namespace VspEngine
{
	/// <summary>
	/// Reference handle for a native transform. Position, rotation and scale are
	/// stored in the native scene; this class only forwards to them, so a
	/// transform shared between scripts always reads the same values.
	/// </summary>
	public sealed class Transform : Object
	{
		// Scratch buffer the native getters fill with x, y, z.
		private readonly float[] xyzScratch = new float[3];

		internal Transform(uint nativeHandle)
		{
			NativeHandle = nativeHandle;
		}

		/// <summary>The game object this transform belongs to (0 when detached).</summary>
		public uint OwnerGameObjectHandle => NativeApi.VspTransform_GetOwnerGameObject(NativeHandle);

		/// <summary>Local position in world units (screen center is the origin).</summary>
		public Vector3 LocalPosition
		{
			get
			{
				NativeApi.VspTransform_GetLocalPosition(NativeHandle, xyzScratch);
				return new Vector3(xyzScratch[0], xyzScratch[1], xyzScratch[2]);
			}
			set => NativeApi.VspTransform_SetLocalPosition(NativeHandle, value.X, value.Y, value.Z);
		}

		/// <summary>
		/// Screen-space position: the X/Y of <see cref="LocalPosition"/>. Kept
		/// because a 2D script almost never needs the depth component.
		/// </summary>
		public Vector2 Position
		{
			get
			{
				Vector3 localPosition = LocalPosition;
				return new Vector2(localPosition.X, localPosition.Y);
			}
			set => NativeApi.VspTransform_SetLocalPosition(NativeHandle, value.X, value.Y, 0.0f);
		}

		/// <summary>Local rotation in Euler angles (degrees).</summary>
		public Vector3 LocalEulerAngles
		{
			get
			{
				NativeApi.VspTransform_GetLocalRotation(NativeHandle, xyzScratch);
				return new Vector3(xyzScratch[0], xyzScratch[1], xyzScratch[2]);
			}
			set => NativeApi.VspTransform_SetLocalRotation(NativeHandle, value.X, value.Y, value.Z);
		}

		/// <summary>Local scale (1, 1, 1 by default).</summary>
		public Vector3 LocalScale
		{
			get
			{
				NativeApi.VspTransform_GetLocalScale(NativeHandle, xyzScratch);
				return new Vector3(xyzScratch[0], xyzScratch[1], xyzScratch[2]);
			}
			set => NativeApi.VspTransform_SetLocalScale(NativeHandle, value.X, value.Y, value.Z);
		}

		/// <summary>True while the transform changed since the flag was cleared.</summary>
		public bool IsDirty => NativeApi.VspTransform_IsDirty(NativeHandle) != 0;

		/// <summary>Clears the change flag (a system that consumed the value does this).</summary>
		public void ClearDirtyFlag() => NativeApi.VspTransform_ClearDirtyFlag(NativeHandle);
	}
}
