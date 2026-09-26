using System;
using System.Numerics;

namespace VspEngine
{
	/// <summary>What a ray found: where it entered a collider and which way that surface faces.</summary>
	public readonly struct RaycastResult
	{
		/// <summary>True when the ray entered something within its range.</summary>
		public readonly bool HasHit;

		/// <summary>Point the ray entered the collider at, in world space.</summary>
		public readonly Vector3 Point;

		/// <summary>Unit surface normal at that point, pointing out of the collider.</summary>
		public readonly Vector3 Normal;

		/// <summary>Distance along the ray direction, in world units.</summary>
		public readonly float Distance;

		/// <summary>The collider that was entered (null when nothing was).</summary>
		public readonly Collider? Collider;

		internal RaycastResult(bool hasHit, Vector3 point, Vector3 normal, float distance, Collider? collider)
		{
			HasHit = hasHit;
			Point = point;
			Normal = normal;
			Distance = distance;
			Collider = collider;
		}

		/// <summary>The empty answer, which is what a ray that hit nothing reports.</summary>
		public static RaycastResult None => new RaycastResult(false, Vector3.Zero, Vector3.Zero, 0.0f, null);
	}

	/// <summary>One overlap the last step resolved, as a script sees it.</summary>
	public readonly struct PhysicsContact
	{
		/// <summary>The first collider of the pair.</summary>
		public readonly Collider? ColliderA;

		/// <summary>The second collider of the pair.</summary>
		public readonly Collider? ColliderB;

		/// <summary>Unit direction that separates B from A.</summary>
		public readonly Vector3 Normal;

		/// <summary>A point inside the overlap, in world space.</summary>
		public readonly Vector3 Point;

		/// <summary>How deep the two shapes overlap, in world units.</summary>
		public readonly float Penetration;

		internal PhysicsContact(Collider? colliderA, Collider? colliderB, Vector3 normal, Vector3 point, float penetration)
		{
			ColliderA = colliderA;
			ColliderB = colliderB;
			Normal = normal;
			Point = point;
			Penetration = penetration;
		}
	}

	/// <summary>What the last physics step did, in numbers.</summary>
	public readonly struct PhysicsStats
	{
		/// <summary>Colliders the step considered.</summary>
		public readonly int ColliderCount;

		/// <summary>Bodies the step integrated.</summary>
		public readonly int DynamicBodyCount;

		/// <summary>Collider pairs whose bounds overlapped and were tested.</summary>
		public readonly int PairCount;

		/// <summary>Contacts the narrowphase found and the solver resolved.</summary>
		public readonly int ContactCount;

		/// <summary>Pairs skipped because no shape test answers them (mesh against mesh).</summary>
		public readonly int SkippedPairCount;

		/// <summary>Steps the world has run since it started.</summary>
		public readonly int StepCount;

		internal PhysicsStats(int colliderCount, int dynamicBodyCount, int pairCount, int contactCount, int skippedPairCount, int stepCount)
		{
			ColliderCount = colliderCount;
			DynamicBodyCount = dynamicBodyCount;
			PairCount = pairCount;
			ContactCount = contactCount;
			SkippedPairCount = skippedPairCount;
			StepCount = stepCount;
		}

		public override string ToString() =>
			"PhysicsStats(colliders " + ColliderCount + ", dynamic bodies " + DynamicBodyCount +
			", pairs " + PairCount + ", contacts " + ContactCount +
			", skipped " + SkippedPairCount + ", steps " + StepCount + ")";
	}

	/// <summary>
	/// The physics world, as a script reaches it: what it simulates, how it is
	/// configured, and the queries a game asks of it.
	///
	/// The engine advances the world once per frame, right after the scripts have
	/// run and right before the frame is drawn, so a script writes velocities and
	/// forces and reads their result back on the next frame. A game that wants to
	/// drive the simulation itself - a fixed-step accumulator, a replay, a paused
	/// editor - sets <see cref="AutoSimulation"/> to false and calls
	/// <see cref="Step"/> where it wants the step to happen.
	/// </summary>
	public static class Physics
	{
		private static readonly float[] xyzScratch = new float[3];
		private static readonly float[] hitScratch = new float[7];
		private static readonly uint[] contactHandleScratch = new uint[2];
		private static readonly float[] contactValueScratch = new float[7];
		private static readonly uint[] statsScratch = new uint[6];

		/// <summary>Gravity every body with <see cref="Rigidbody.UseGravity"/> follows, in world units per second squared.</summary>
		public static Vector3 Gravity
		{
			get
			{
				NativeApi.VspPhysics_GetGravity(xyzScratch);
				return new Vector3(xyzScratch[0], xyzScratch[1], xyzScratch[2]);
			}
			set => NativeApi.VspPhysics_SetGravity(value.X, value.Y, value.Z);
		}

		/// <summary>
		/// Whether the engine advances the world once per frame. True by default;
		/// turn it off to take the step over.
		/// </summary>
		public static bool AutoSimulation
		{
			get => NativeApi.VspPhysics_GetAutoSimulation() != 0;
			set => NativeApi.VspPhysics_SetAutoSimulation(value ? 1 : 0);
		}

		/// <summary>Passes the solver makes over the contacts of one step.</summary>
		public static int SolverIterationCount
		{
			get => (int)NativeApi.VspPhysics_GetSolverIterationCount();
			set => NativeApi.VspPhysics_SetSolverIterationCount((uint)(value > 0 ? value : 1));
		}

		/// <summary>Advances the simulation by one step of the given length.</summary>
		public static void Step(float deltaTime) => NativeApi.VspPhysics_Step(deltaTime);

		/// <summary>
		/// The nearest collider a ray enters, or <see cref="RaycastResult.None"/>
		/// when it enters none. The direction does not have to be normalized.
		/// </summary>
		/// <param name="ignoredObject">
		/// A game object the ray must pass through - every collider of it. A
		/// third-person camera uses it to look past the object it follows, which
		/// the ray would otherwise hit at once on its way out.
		/// </param>
		public static RaycastResult Raycast(
			Vector3 origin,
			Vector3 direction,
			float maximumDistance,
			GameObject? ignoredObject = null)
		{
			uint ignoredHandle = ignoredObject != null ? ignoredObject.NativeHandle : 0;
			uint colliderHandle = NativeApi.VspPhysics_Raycast(
				origin.X, origin.Y, origin.Z,
				direction.X, direction.Y, direction.Z,
				maximumDistance,
				ignoredHandle,
				hitScratch);

			if (colliderHandle == 0)
			{
				return RaycastResult.None;
			}

			return new RaycastResult(
				true,
				new Vector3(hitScratch[0], hitScratch[1], hitScratch[2]),
				new Vector3(hitScratch[3], hitScratch[4], hitScratch[5]),
				hitScratch[6],
				new Collider(colliderHandle));
		}

		/// <summary>Contacts the last step resolved.</summary>
		public static int ContactCount => (int)NativeApi.VspPhysics_GetContactCount();

		/// <summary>One contact of the last step.</summary>
		public static PhysicsContact GetContact(int contactIndex)
		{
			NativeApi.VspPhysics_GetContact((uint)contactIndex, contactHandleScratch, contactValueScratch);
			return new PhysicsContact(
				contactHandleScratch[0] != 0 ? new Collider(contactHandleScratch[0]) : null,
				contactHandleScratch[1] != 0 ? new Collider(contactHandleScratch[1]) : null,
				new Vector3(contactValueScratch[0], contactValueScratch[1], contactValueScratch[2]),
				new Vector3(contactValueScratch[3], contactValueScratch[4], contactValueScratch[5]),
				contactValueScratch[6]);
		}

		/// <summary>What the last step did.</summary>
		public static PhysicsStats Stats
		{
			get
			{
				NativeApi.VspPhysics_GetStats(statsScratch);
				return new PhysicsStats(
					(int)statsScratch[0],
					(int)statsScratch[1],
					(int)statsScratch[2],
					(int)statsScratch[3],
					(int)statsScratch[4],
					(int)statsScratch[5]);
			}
		}
	}
}
