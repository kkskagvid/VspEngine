using System;
using System.Collections.Generic;
using System.Numerics;

namespace VspEngine
{
	/// <summary>
	/// Reference handle for a native rigidbody: the motion of a game object.
	///
	/// A body owns a velocity, an angular velocity and the settings that decide
	/// how they change - mass, gravity, drag - and the engine's physics world
	/// integrates them into the transform of the object the body belongs to.
	///
	/// A body MOVES an object; a <see cref="Collider"/> says what that object IS.
	/// An object with a collider and no body never moves and is what other bodies
	/// collide with, which is how a level is built.
	///
	/// The engine advances the world once per frame, after the scripts have run
	/// (see <see cref="Physics.AutoSimulation"/>), so a script sets a velocity
	/// here and reads the result back on the next frame.
	/// </summary>
	public sealed class Rigidbody : Object
	{
		private readonly float[] xyzScratch = new float[3];

		internal Rigidbody(uint nativeHandle)
		{
			NativeHandle = nativeHandle;
		}

		/// <summary>Gives a game object a body, which makes the simulation move it.</summary>
		public static Rigidbody? Create(GameObject gameObject)
		{
			if (gameObject == null)
			{
				throw new ArgumentNullException(nameof(gameObject));
			}

			uint nativeHandle = NativeApi.VspRigidbody_Create(gameObject.NativeHandle);
			return nativeHandle != 0 ? new Rigidbody(nativeHandle) : null;
		}

		/// <summary>Every rigidbody of the scene, in the order the scene stores them.</summary>
		public static Rigidbody[] All()
		{
			int bodyCount = (int)NativeApi.VspScene_GetLiveRigidbodyCount();
			List<Rigidbody> bodies = new List<Rigidbody>(bodyCount);
			for (uint bodyIndex = 0; bodyIndex < bodyCount; ++bodyIndex)
			{
				uint bodyHandle = NativeApi.VspScene_GetRigidbodyHandle(bodyIndex);
				if (bodyHandle != 0)
				{
					bodies.Add(new Rigidbody(bodyHandle));
				}
			}
			return bodies.ToArray();
		}

		/// <summary>
		/// The rigidbody of a game object, or null when it has none.
		/// </summary>
		public static Rigidbody? Get(GameObject gameObject)
		{
			if (gameObject == null)
			{
				throw new ArgumentNullException(nameof(gameObject));
			}

			uint gameObjectHandle = gameObject.NativeHandle;
			foreach (Rigidbody body in All())
			{
				if (NativeApi.VspRigidbody_GetGameObject(body.NativeHandle) == gameObjectHandle)
				{
					return body;
				}
			}
			return null;
		}

		/// <summary>The game object this body moves.</summary>
		public GameObject? OwnerGameObject
		{
			get
			{
				uint gameObjectHandle = NativeApi.VspRigidbody_GetGameObject(NativeHandle);
				return gameObjectHandle != 0 ? new GameObject(gameObjectHandle) : null;
			}
		}

		// -----------------------------------------------------------------
		// What drives the body
		// -----------------------------------------------------------------

		/// <summary>
		/// How much matter the object has. 0 makes it immovable, which is what a
		/// static body is; the same impulse then moves a light body further than a
		/// heavy one.
		/// </summary>
		public float Mass
		{
			get => NativeApi.VspRigidbody_GetMass(NativeHandle);
			set => NativeApi.VspRigidbody_SetMass(NativeHandle, value);
		}

		/// <summary>1 / mass, and 0 for a body the simulation may not move.</summary>
		public float InverseMass => NativeApi.VspRigidbody_GetInverseMass(NativeHandle);

		/// <summary>
		/// A kinematic body is placed by its transform instead of by the
		/// simulation: it pushes other bodies and nothing pushes it.
		/// </summary>
		public bool IsKinematic
		{
			get => NativeApi.VspRigidbody_IsKinematic(NativeHandle) != 0;
			set => NativeApi.VspRigidbody_SetKinematic(NativeHandle, value ? 1 : 0);
		}

		/// <summary>Whether the world's gravity pulls this body down.</summary>
		public bool UseGravity
		{
			get => NativeApi.VspRigidbody_GetUseGravity(NativeHandle) != 0;
			set => NativeApi.VspRigidbody_SetUseGravity(NativeHandle, value ? 1 : 0);
		}

		/// <summary>Multiplier on the world's gravity: 0 floats, 2 falls twice as fast.</summary>
		public float GravityScale
		{
			get => NativeApi.VspRigidbody_GetGravityScale(NativeHandle);
			set => NativeApi.VspRigidbody_SetGravityScale(NativeHandle, value);
		}

		// -----------------------------------------------------------------
		// Motion
		// -----------------------------------------------------------------

		/// <summary>How fast the object moves, in world units per second.</summary>
		public Vector3 Velocity
		{
			get
			{
				NativeApi.VspRigidbody_GetVelocity(NativeHandle, xyzScratch);
				return new Vector3(xyzScratch[0], xyzScratch[1], xyzScratch[2]);
			}
			set => NativeApi.VspRigidbody_SetVelocity(NativeHandle, value.X, value.Y, value.Z);
		}

		/// <summary>How fast the object turns about each of its own axes, in degrees per second.</summary>
		public Vector3 AngularVelocity
		{
			get
			{
				NativeApi.VspRigidbody_GetAngularVelocity(NativeHandle, xyzScratch);
				return new Vector3(xyzScratch[0], xyzScratch[1], xyzScratch[2]);
			}
			set => NativeApi.VspRigidbody_SetAngularVelocity(NativeHandle, value.X, value.Y, value.Z);
		}

		/// <summary>Stops the simulation from turning the object.</summary>
		public bool FreezeRotation
		{
			get => NativeApi.VspRigidbody_GetFreezeRotation(NativeHandle) != 0;
			set => NativeApi.VspRigidbody_SetFreezeRotation(NativeHandle, value ? 1 : 0);
		}

		/// <summary>How quickly motion dies down on its own: 0 keeps it forever.</summary>
		public float LinearDrag
		{
			get => NativeApi.VspRigidbody_GetLinearDrag(NativeHandle);
			set => NativeApi.VspRigidbody_SetLinearDrag(NativeHandle, value);
		}

		/// <summary>The same, for the turn rate.</summary>
		public float AngularDrag
		{
			get => NativeApi.VspRigidbody_GetAngularDrag(NativeHandle);
			set => NativeApi.VspRigidbody_SetAngularDrag(NativeHandle, value);
		}

		// -----------------------------------------------------------------
		// Forces
		// -----------------------------------------------------------------

		/// <summary>Adds a force, which the next step spreads over its whole duration.</summary>
		public void AddForce(Vector3 force) =>
			NativeApi.VspRigidbody_AddForce(NativeHandle, force.X, force.Y, force.Z);

		/// <summary>Adds an impulse, which changes the velocity at once.</summary>
		public void AddImpulse(Vector3 impulse) =>
			NativeApi.VspRigidbody_AddImpulse(NativeHandle, impulse.X, impulse.Y, impulse.Z);

		// -----------------------------------------------------------------
		// Resting
		// -----------------------------------------------------------------

		/// <summary>
		/// True while the body is left out of the simulation because it has been
		/// still for long enough. Anything that pushes it wakes it again.
		/// </summary>
		public bool IsSleeping => NativeApi.VspRigidbody_IsSleeping(NativeHandle) != 0;

		/// <summary>
		/// True while a contact pushed the body up during the last step, which is
		/// what a script checks before it lets a character jump.
		/// </summary>
		public bool IsGrounded => NativeApi.VspRigidbody_IsGrounded(NativeHandle) != 0;

		/// <summary>Brings a sleeping body back into the simulation.</summary>
		public void Wake() => NativeApi.VspRigidbody_Wake(NativeHandle);

		/// <summary>Puts the body to sleep at once: its motion stops.</summary>
		public void Sleep() => NativeApi.VspRigidbody_Sleep(NativeHandle);

		/// <summary>Releases the native body; the object it moved stays where it is.</summary>
		public void Destroy()
		{
			NativeApi.VspRigidbody_Destroy(NativeHandle);
			NativeHandle = 0;
		}
	}
}
