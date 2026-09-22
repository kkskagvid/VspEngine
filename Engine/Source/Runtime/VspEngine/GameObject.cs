using System;

namespace VspEngine
{
	/// <summary>
	/// A scene object. The managed instance is a reference handle: the object's
	/// name, activation state, layer, transform and components all live in the
	/// native scene, and every property here forwards to them.
	///
	/// Objects form a hierarchy through their transforms
	/// (<see cref="Transform.SetParent(Transform?, bool)"/>), which is what turns
	/// local coordinates into world coordinates.
	/// </summary>
	public class GameObject : Object
	{
		private Transform? cachedTransform;

		internal GameObject(uint nativeHandle)
		{
			NativeHandle = nativeHandle;
		}

		/// <summary>Creates a new native scene object and wraps its handle.</summary>
		public static GameObject? Create(string name)
		{
			uint nativeHandle = NativeApi.VspGameObject_Create(name);
			return nativeHandle != 0 ? new GameObject(nativeHandle) : null;
		}

		/// <summary>Whether this object itself is active (ignores its parents).</summary>
		public bool ActiveSelf
		{
			get => NativeApi.VspGameObject_GetActiveSelf(NativeHandle) != 0;
			set => NativeApi.VspGameObject_SetActiveSelf(NativeHandle, value ? 1 : 0);
		}

		/// <summary>Layer index the object belongs to (used for filtering).</summary>
		public int Layer
		{
			get => NativeApi.VspGameObject_GetLayer(NativeHandle);
			set => NativeApi.VspGameObject_SetLayer(NativeHandle, value);
		}

		/// <summary>The transform every game object owns.</summary>
		public Transform Transform
		{
			get
			{
				if (cachedTransform == null)
				{
					cachedTransform = new Transform(NativeApi.VspGameObject_GetTransform(NativeHandle));
				}
				return cachedTransform;
			}
		}

		/// <summary>True while the object still owns a live transform.</summary>
		public bool HasValidTransform => NativeApi.VspGameObject_GetTransform(NativeHandle) != 0;

		/// <summary>
		/// The game object this one is parented to, or null when it is a scene
		/// root. The link lives on the transform, so this follows it.
		/// </summary>
		public GameObject? Parent
		{
			get
			{
				uint transformHandle = NativeApi.VspGameObject_GetTransform(NativeHandle);
				if (transformHandle == 0)
				{
					return null;
				}

				uint parentTransformHandle = NativeApi.VspTransform_GetParent(transformHandle);
				if (parentTransformHandle == 0)
				{
					return null;
				}

				uint parentGameObjectHandle = NativeApi.VspTransform_GetOwnerGameObject(parentTransformHandle);
				return parentGameObjectHandle != 0 ? new GameObject(parentGameObjectHandle) : null;
			}
		}

		/// <summary>Releases the native object, its transform and its components.</summary>
		public void Destroy()
		{
			NativeApi.VspGameObject_Destroy(NativeHandle);
			NativeHandle = 0;
			cachedTransform = null;
		}
	}
}
