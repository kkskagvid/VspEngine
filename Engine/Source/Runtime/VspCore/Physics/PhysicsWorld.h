#pragma once

#include "Core/Core.h"
#include "Core/Templates/ArrayList.h"
#include "Math/Vector3.h"
#include "Physics/CollisionDetection.h"
#include "Physics/PhysicsTypes.h"

namespace Vsp
{
	// -------------------------------------------------------------------------
	// PhysicsStepStats
	// -------------------------------------------------------------------------
	// What the last step did, in numbers. A test asserts on them, a diagnostics
	// overlay prints them, and a scene that is slower than it should be is
	// diagnosed from them.
	// -------------------------------------------------------------------------
	struct PhysicsStepStats
	{
		// Colliders the step considered.
		uint32 uColliderCount = 0;

		// Bodies the step integrated (dynamic, awake ones).
		uint32 uDynamicBodyCount = 0;

		// Collider pairs whose world bounds overlapped and that the narrowphase
		// therefore tested.
		uint32 uPairCount = 0;

		// Contacts the narrowphase found and the solver resolved.
		uint32 uContactCount = 0;

		// Pairs skipped because no narrowphase answers them (see Step).
		uint32 uSkippedPairCount = 0;

		// Steps the world has run since it was created.
		uint32 uStepCount = 0;
	};

	// -------------------------------------------------------------------------
	// PhysicsWorld
	// -------------------------------------------------------------------------
	// The simulation: one step turns the bodies the scene holds into the places
	// they move to, and the contacts that stopped them.
	//
	// A step is five passes over data the scene already owns:
	//
	//   gather     every collider and every rigidbody of the scene, with the
	//              world shape the collider's transform produces
	//   integrate  gravity, accumulated forces and drag, then the positions and
	//              the rotations that follow from the velocities
	//   broadphase collider pairs whose world BOUNDS overlap; everything else is
	//              provably apart and never reaches the narrowphase
	//   narrowphase the shape tests of CollisionDetection, producing contacts
	//   solve      positional correction plus sequential impulses, then the
	//              sleeping bookkeeping
	//
	// The world owns no objects of its own: it is a service over Classes/Scene,
	// and a collider or a body that was destroyed simply stops being gathered.
	//
	// DOCUMENTED SIMPLIFICATIONS of this first simulation step, so a reader knows
	// where the model ends:
	//   * contacts are resolved with LINEAR impulses only - a contact never
	//     torques a body, so an object turns only through its angular velocity.
	//     A resting box therefore cannot tip over, which is what keeps the demo's
	//     spinning cube stable;
	//   * a mesh collider is level geometry: mesh-against-mesh pairs have no
	//     narrowphase and are counted in uSkippedPairCount instead;
	//   * the broadphase is a plain sweep over all pairs, which is right for the
	//     scenes this engine renders and wants a spatial partition beyond that.
	//
	// Every function is a plain data operation; nothing here throws, and a step
	// that cannot do something (a body whose object disappeared, a shape with no
	// volume) skips it and keeps going.
	// -------------------------------------------------------------------------
#pragma warning(push)
#pragma warning(disable : 4251)   // ArrayList members: header-only template.
	class RUNTIME_API PhysicsWorld
	{
	public:
		// Gravity of the default world: Earth's, in world units per second
		// squared, pointing down the engine's -Y.
		static constexpr float k_fEarthGravity = -9.81f;

		// Solver passes over the contacts per step. More passes make a stack of
		// bodies settle more firmly and cost proportionally more.
		static constexpr uint32 k_nDefaultSolverIterationCount = 4;

		// Longest step the world simulates. A frame that took longer than this
		// (a breakpoint, a stalled device, a window being dragged) advances the
		// simulation by this much instead: a body can then never be teleported
		// through a wall by one enormous step.
		static constexpr float k_fMaximumStepSeconds = 0.05f;

		static PhysicsWorld& Get();

		// -------- Settings --------
		const Vector3& GetGravity() const { return m_Gravity; }
		void SetGravity(const Vector3& Gravity) { m_Gravity = Gravity; }

		uint32 GetSolverIterationCount() const { return m_uSolverIterationCount; }
		void SetSolverIterationCount(uint32 uSolverIterationCount);

		// True while the engine host advances the world once per frame, after the
		// scripts have run. A game that wants to drive the simulation itself - a
		// fixed-step accumulator, a replay, a paused editor - turns it off and
		// calls Step() where it wants the step to happen.
		bool IsAutoSimulationEnabled() const { return m_bAutoSimulationEnabled; }
		void SetAutoSimulationEnabled(bool bAutoSimulationEnabled) { m_bAutoSimulationEnabled = bAutoSimulationEnabled; }

		float GetMaximumStepSeconds() const { return m_fMaximumStepSeconds; }
		void SetMaximumStepSeconds(float fMaximumStepSeconds);

		// -------- Simulation --------
		// Advances the world by fDeltaSeconds. A step of zero or less, or a world
		// with nothing in it, is a no-op that still counts the step.
		void Step(float fDeltaSeconds);

		// Forgets the contacts, the caches and the statistics (a scene reset).
		void Clear();

		// -------- Queries --------
		// The nearest collider the ray enters within fMaximumDistance.
		//
		// Every collider of the game object named by uIgnoredGameObjectHandle is
		// skipped, which is how a third-person camera looks past the object it
		// follows: the ray towards the camera leaves that object from the inside.
		//
		// The direction is normalized here, so a caller may pass any non-zero
		// vector.
		bool Raycast(
			const Vector3& Origin,
			const Vector3& Direction,
			float fMaximumDistance,
			PhysicsRaycastHit& outHit,
			NativeObjectHandle uIgnoredGameObjectHandle = k_nInvalidObjectHandle);

		// Contacts the last step resolved, in the order they were found.
		uint32 GetContactCount() const { return static_cast<uint32>(m_Contacts.GetSize()); }
		const PhysicsContact& GetContact(uint32 uContactIndex) const { return m_Contacts[uContactIndex]; }

		const PhysicsStepStats& GetStats() const { return m_Stats; }

	private:
		// The registry in Core/EngineServices.h owns this service's storage and
		// lifetime, so it has to be able to construct it.
		friend class EngineServices;

		PhysicsWorld() = default;

		// One body, as the step works on it. The values are read from the
		// Rigidbody at the start of the step and written back at its end, so the
		// simulation never holds a scene pointer across a table lookup.
		struct BodyState
		{
			NativeObjectHandle uRigidbodyHandle = k_nInvalidObjectHandle;
			NativeObjectHandle uTransformHandle = k_nInvalidObjectHandle;

			Vector3 Position = Vector3::Zero;
			Vector3 EulerDegrees = Vector3::Zero;
			Vector3 Velocity = Vector3::Zero;
			Vector3 AngularVelocity = Vector3::Zero;
			Vector3 AccumulatedForce = Vector3::Zero;

			float fInverseMass = 0.0f;
			float fGravityScale = 1.0f;
			float fLinearDrag = 0.0f;
			float fAngularDrag = 0.0f;
			float fRestingSeconds = 0.0f;

			bool bUseGravity = true;
			bool bIsKinematic = false;
			bool bFreezeRotation = false;
			bool bIsSleeping = false;
			bool bIsGrounded = false;
		};

		// One collider of the step: its world shape, its bounds and the body that
		// moves it (none, for level geometry).
		struct ColliderState
		{
			NativeObjectHandle uColliderHandle = k_nInvalidObjectHandle;
			NativeObjectHandle uOwnerGameObjectHandle = k_nInvalidObjectHandle;
			uint32 uBodyIndex = 0;

			ColliderShape eShape = ColliderShape::Box;
			CollisionBox Box;
			CollisionSphere Sphere;

			// Range the mesh of a mesh collider occupies in the world-space pools.
			uint32 uMeshVertexFirst = 0;
			uint32 uMeshVertexCount = 0;
			uint32 uMeshIndexFirst = 0;
			uint32 uMeshIndexCount = 0;

			Vector3 BoundsMinimum = Vector3::Zero;
			Vector3 BoundsMaximum = Vector3::Zero;

			float fRestitution = 0.0f;
			float fFriction = 0.5f;

			bool bHasBody = false;
		};

		void ResetStepCaches();
		void GatherBodies();
		void IntegrateBodies(float fDeltaSeconds);
		void GatherColliders();

		// Rebuilds the collider cache on its own, for a query asked outside a step.
		void RefreshColliderCache();
		void FindContacts();
		void ResolveContacts();
		void FinishBodies(float fDeltaSeconds);

		// Writes a body's place into the transform it drives. Called as soon as a
		// position changes, so a later collider lookup sees where the body is.
		void WriteBodyTransform(const BodyState& state);

		// The state of one collider, by handle; an empty state when the step did
		// not gather it.
		const ColliderState& FindColliderState(NativeObjectHandle uColliderHandle) const;

		// Builds the world-space mesh of one mesh collider into the shared pools
		// and returns the state that addresses it.
		void AppendMeshCollider(ColliderState& outState, const class Collider& collider);

		// The collider state the narrowphase needs for one state, as the borrowed
		// view CollisionDetection takes.
		CollisionMesh GetCollisionMesh(const ColliderState& state) const;

		// Applies the contact's impulse to the two bodies it involves; the
		// positional correction is a separate pass so that iterating the impulses
		// cannot push a body out four times over.
		void ApplyContactImpulse(const PhysicsContact& contact, const ColliderState& stateA, const ColliderState& stateB);
		void ApplyContactCorrection(const PhysicsContact& contact, const ColliderState& stateA, const ColliderState& stateB);

		// Index of the body that drives a game object, or 0 with bHasBody false.
		uint32 FindBodyIndex(NativeObjectHandle uGameObjectHandle, bool& bHasBody) const;

		Vector3 m_Gravity = Vector3(0.0f, k_fEarthGravity, 0.0f);
		uint32 m_uSolverIterationCount = k_nDefaultSolverIterationCount;
		float m_fMaximumStepSeconds = k_fMaximumStepSeconds;
		bool m_bAutoSimulationEnabled = true;

		ArrayList<BodyState> m_BodyStates;
		ArrayList<ColliderState> m_ColliderStates;
		ArrayList<PhysicsContact> m_Contacts;

		// World-space geometry of every mesh collider of the step. The states
		// address their own range, so gathering allocates nothing after warmup.
		ArrayList<Vector3> m_MeshWorldVertices;
		ArrayList<uint32> m_MeshWorldIndices;

		PhysicsStepStats m_Stats;
	};
#pragma warning(pop)
}
