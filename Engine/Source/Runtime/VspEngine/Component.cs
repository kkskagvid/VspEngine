using System;

namespace VspEngine
{
	/// <summary>
	/// Base class of everything that can be attached to a <see cref="GameObject"/>.
	///
	/// The managed instance is a reference handle for the native component: the
	/// owner, the enable state and the render state the render pipeline reads
	/// all live in the native scene. The wrapper objects handed out below are
	/// reference handles too; their IsValid tells whether the native object is
	/// still alive.
	/// </summary>
	public abstract class Component : Object
	{
		private GameObject? cachedGameObject;
		private Transform? cachedTransform;

		/// <summary>Whether the component takes part in updates and rendering.</summary>
		public bool Enabled
		{
			get => NativeApi.VspComponent_IsEnabled(NativeHandle) != 0;
			set => NativeApi.VspComponent_SetEnabled(NativeHandle, value ? 1 : 0);
		}

		/// <summary>
		/// Game object this component is attached to. Its handle is 0 (and
		/// IsValid false) once the component was detached or destroyed, so the
		/// accessor itself never returns null.
		/// </summary>
		public GameObject GameObject
		{
			get
			{
				cachedGameObject ??= new GameObject(NativeApi.VspComponent_GetOwnerGameObject(NativeHandle));
				return cachedGameObject;
			}
		}

		/// <summary>Transform of the owning game object (invalid when detached).</summary>
		public Transform Transform
		{
			get
			{
				cachedTransform ??= new Transform(NativeApi.VspGameObject_GetTransform(GameObject.NativeHandle));
				return cachedTransform;
			}
		}

		/// <summary>
		/// Pre-seeds the owner links the C++ host already resolved when it
		/// created the component, so the first access does not have to ask the
		/// native scene again.
		/// </summary>
		internal void OnHandlesAssigned(uint gameObjectHandle, uint transformHandle)
		{
			if (gameObjectHandle != 0)
			{
				cachedGameObject = new GameObject(gameObjectHandle);
			}

			if (transformHandle != 0)
			{
				cachedTransform = new Transform(transformHandle);
			}
		}
	}
}
