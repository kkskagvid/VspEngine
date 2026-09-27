#include "RuntimePCH.h"

#include <cmath>

#include "Classes/Collider.h"
#include "Classes/GameObject.h"
#include "Classes/Rigidbody.h"
#include "Classes/Scene.h"
#include "Classes/Transform.h"
#include "Core/EngineServices.h"
#include "Core/Logging/Log.h"
#include "Physics/PhysicsWorld.h"

namespace Vsp
{
	namespace
	{
		// Index a collider state carries when no body drives it.
		constexpr uint32 k_nNoBodyIndex = UINT32_MAX;

		// Below this length a ray direction has no direction to cast along.
		constexpr float k_fMinimumDirectionLength = 1e-6f;

		// Tangential speed below which a contact applies no friction: at rest the
		// tangential velocity is numerical noise, and pushing against noise is
		// what makes a resting body creep.
		constexpr float k_fMinimumTangentialSpeed = 1e-4f;

		// How far a contact normal has to point up before the body it pushes is
		// reported as standing on something.
		constexpr float k_fGroundedNormalThreshold = 0.5f;

		// Speed above which a contact wakes a body that was asleep. Below it the
		// contact is a resting one, and waking on those would defeat sleeping.
		constexpr float k_fWakeSpeedThreshold = 0.05f;

		// Velocity multiplier a drag value produces over one step. Written as a
		// division rather than as (1 - drag * dt) so that a large drag or a long
		// step can never turn into a negative - i.e. reversed - velocity.
		float ComputeDragFactor(float fDrag, float fDeltaSeconds)
		{
			const float fDenominator = 1.0f + (fDrag * fDeltaSeconds);
			return (fDenominator > 0.0f) ? (1.0f / fDenominator) : 0.0f;
		}

		bool DoBoundsOverlap(const Vector3& MinimumA, const Vector3& MaximumA, const Vector3& MinimumB, const Vector3& MaximumB)
		{
			return (MinimumA.fX <= MaximumB.fX) && (MaximumA.fX >= MinimumB.fX) &&
				(MinimumA.fY <= MaximumB.fY) && (MaximumA.fY >= MinimumB.fY) &&
				(MinimumA.fZ <= MaximumB.fZ) && (MaximumA.fZ >= MinimumB.fZ);
		}

		// The surface two colliders present to each other. Restitution combines as
		// the bouncier of the two (a rubber ball on concrete bounces), friction as
		// the geometric mean (a slippery surface makes any pair slippery, without
		// either one alone deciding it).
		float CombineRestitution(float fRestitutionA, float fRestitutionB)
		{
			return (fRestitutionA > fRestitutionB) ? fRestitutionA : fRestitutionB;
		}

		float CombineFriction(float fFrictionA, float fFrictionB)
		{
			return std::sqrt(fFrictionA * fFrictionB);
		}
	}

	PhysicsWorld& PhysicsWorld::Get()
	{
		// The registry owns this service: it is created here on first use,
		// reports a lookup from any thread but the one that created it, and is
		// destroyed explicitly by EngineServices::ShutdownAll().
		return EngineServices::GetService<PhysicsWorld>("PhysicsWorld");
	}

	void PhysicsWorld::SetSolverIterationCount(uint32 uSolverIterationCount)
	{
		// At least one pass is what "resolve the contacts" means; more than a
		// handful stops changing the result and costs linearly.
		m_uSolverIterationCount = (uSolverIterationCount > 0u) ? uSolverIterationCount : 1u;
	}

	void PhysicsWorld::SetMaximumStepSeconds(float fMaximumStepSeconds)
	{
		m_fMaximumStepSeconds = (fMaximumStepSeconds > 0.0f) ? fMaximumStepSeconds : k_fMaximumStepSeconds;
	}

	// -------------------------------------------------------------------------
	// The step
	// -------------------------------------------------------------------------

	void PhysicsWorld::Step(float fDeltaSeconds)
	{
		++m_Stats.uStepCount;
		ResetStepCaches();

		// A frame that did not advance time - the first one, a paused clock, a
		// delta a caller could not measure - simulates nothing and keeps the
		// scene exactly as it is.
		if (!(fDeltaSeconds > 0.0f))
		{
			return;
		}
		if (fDeltaSeconds > m_fMaximumStepSeconds)
		{
			fDeltaSeconds = m_fMaximumStepSeconds;
		}

		GatherBodies();
		IntegrateBodies(fDeltaSeconds);
		GatherColliders();
		m_Stats.uColliderCount = static_cast<uint32>(m_ColliderStates.GetSize());
		FindContacts();
		ResolveContacts();
		FinishBodies(fDeltaSeconds);
	}

	void PhysicsWorld::Clear()
	{
		ResetStepCaches();
		m_Stats = PhysicsStepStats();
	}

	void PhysicsWorld::ResetStepCaches()
	{
		m_BodyStates.Clear();
		m_ColliderStates.Clear();
		m_Contacts.Clear();
		m_MeshWorldVertices.Clear();
		m_MeshWorldIndices.Clear();

		m_Stats.uColliderCount = 0;
		m_Stats.uDynamicBodyCount = 0;
		m_Stats.uPairCount = 0;
		m_Stats.uContactCount = 0;
		m_Stats.uSkippedPairCount = 0;
	}

	// -------------------------------------------------------------------------
	// Gather
	// -------------------------------------------------------------------------

	void PhysicsWorld::GatherBodies()
	{
		Scene& scene = Scene::Get();
		const uint32 uLiveBodyCount = scene.GetLiveRigidbodyCount();
		m_BodyStates.Reserve(uLiveBodyCount);

		for (uint32 uBodyIndex = 0u; uBodyIndex < uLiveBodyCount; ++uBodyIndex)
		{
			Rigidbody* pRigidbody = scene.FindRigidbody(scene.GetLiveRigidbodyHandle(uBodyIndex));
			if (pRigidbody == nullptr)
			{
				continue;
			}

			const NativeObjectHandle uTransformHandle = pRigidbody->GetTransformHandle();
			Transform* pTransform = scene.FindTransform(uTransformHandle);
			if (pTransform == nullptr)
			{
				continue;   // The object the body belonged to is gone.
			}

			BodyState state;
			state.uRigidbodyHandle = pRigidbody->GetHandle();
			state.uTransformHandle = uTransformHandle;
			state.Velocity = pRigidbody->GetVelocity();
			state.AngularVelocity = pRigidbody->GetAngularVelocity();
			state.AccumulatedForce = pRigidbody->GetAccumulatedForce();
			state.fGravityScale = pRigidbody->GetGravityScale();
			state.fLinearDrag = pRigidbody->GetLinearDrag();
			state.fAngularDrag = pRigidbody->GetAngularDrag();
			state.fRestingSeconds = pRigidbody->GetRestingSeconds();
			state.bUseGravity = pRigidbody->GetUseGravity();
			state.bIsKinematic = pRigidbody->IsKinematic();
			state.bFreezeRotation = pRigidbody->GetFreezeRotation();
			state.bIsSleeping = pRigidbody->IsSleeping();

			// A kinematic body is placed by its transform, never by the solver, so
			// it acts on contacts as an object of infinite mass.
			state.fInverseMass = state.bIsKinematic ? 0.0f : pRigidbody->GetInverseMass();

			// A body drives the object's LOCAL transform. The demo's bodies are
			// scene roots, where local and world coincide; a body under a moving
			// parent is a hierarchy question for the next step of the simulation.
			pTransform->GetLocalPosition(state.Position.fX, state.Position.fY, state.Position.fZ);
			pTransform->GetLocalRotation(state.EulerDegrees.fX, state.EulerDegrees.fY, state.EulerDegrees.fZ);

			state.bIsGrounded = false;
			m_BodyStates.Add(state);
		}
	}

	void PhysicsWorld::IntegrateBodies(float fDeltaSeconds)
	{
		for (size_t nBodyIndex = 0; nBodyIndex < m_BodyStates.GetSize(); ++nBodyIndex)
		{
			BodyState& state = m_BodyStates[nBodyIndex];
			if (state.fInverseMass <= 0.0f || state.bIsSleeping)
			{
				// Static, kinematic and sleeping bodies keep the place their
				// transform (or the step that put them to sleep) gave them.
				continue;
			}

			// Acceleration from the forces a script accumulated, plus gravity.
			Vector3 acceleration = state.AccumulatedForce * state.fInverseMass;
			if (state.bUseGravity)
			{
				acceleration += m_Gravity * state.fGravityScale;
			}

			// Semi-implicit Euler: the velocity is updated first and the position
			// follows the NEW velocity, which is what makes a body fall the way
			// gravity says rather than one step behind it.
			state.Velocity += acceleration * fDeltaSeconds;
			state.Velocity *= ComputeDragFactor(state.fLinearDrag, fDeltaSeconds);
			state.AngularVelocity *= ComputeDragFactor(state.fAngularDrag, fDeltaSeconds);

			state.Position += state.Velocity * fDeltaSeconds;
			if (!state.bFreezeRotation)
			{
				state.EulerDegrees += state.AngularVelocity * fDeltaSeconds;
			}

			++m_Stats.uDynamicBodyCount;
		}

		// The transforms are written before the colliders are gathered, so every
		// shape this step tests is where the body just moved it to.
		for (size_t nBodyIndex = 0; nBodyIndex < m_BodyStates.GetSize(); ++nBodyIndex)
		{
			const BodyState& state = m_BodyStates[nBodyIndex];
			if (state.fInverseMass <= 0.0f || state.bIsSleeping)
			{
				continue;
			}

			Transform* pTransform = Scene::Get().FindTransform(state.uTransformHandle);
			if (pTransform == nullptr)
			{
				continue;
			}

			pTransform->SetLocalPosition(state.Position.fX, state.Position.fY, state.Position.fZ);
			if (!state.bFreezeRotation)
			{
				pTransform->SetLocalRotation(state.EulerDegrees.fX, state.EulerDegrees.fY, state.EulerDegrees.fZ);
			}
		}
	}

	void PhysicsWorld::GatherColliders()
	{
		Scene& scene = Scene::Get();
		const uint32 uLiveColliderCount = scene.GetLiveColliderCount();
		m_ColliderStates.Reserve(uLiveColliderCount);

		for (uint32 uColliderIndex = 0u; uColliderIndex < uLiveColliderCount; ++uColliderIndex)
		{
			Collider* pCollider = scene.FindCollider(scene.GetLiveColliderHandle(uColliderIndex));
			if (pCollider == nullptr)
			{
				continue;
			}

			// A collider whose object is gone or switched off is not part of the
			// simulation, exactly as it is not part of the frame.
			const GameObject* pOwner = scene.FindGameObject(pCollider->GetOwnerGameObjectHandle());
			if (pOwner == nullptr || !pOwner->IsActiveSelf())
			{
				continue;
			}

			ColliderState state;
			state.uColliderHandle = pCollider->GetHandle();
			state.uOwnerGameObjectHandle = pOwner->GetHandle();
			state.eShape = pCollider->GetShape();
			state.fRestitution = pCollider->GetRestitution();
			state.fFriction = pCollider->GetFriction();
			state.Box = pCollider->GetWorldBox();
			state.Sphere = pCollider->GetWorldSphere();
			if (state.eShape == ColliderShape::Mesh)
			{
				AppendMeshCollider(state, *pCollider);
			}
			pCollider->GetWorldBounds(state.BoundsMinimum, state.BoundsMaximum);

			state.uBodyIndex = FindBodyIndex(state.uOwnerGameObjectHandle, state.bHasBody);

			m_ColliderStates.Add(state);
		}
	}

	void PhysicsWorld::RefreshColliderCache()
	{
		// Only the colliders are rebuilt: the bodies, the contacts and the statistics
		// of the last step stay exactly as the step left them, so a query cannot
		// change what the frame reports about itself.
		m_ColliderStates.Clear();
		m_MeshWorldVertices.Clear();
		m_MeshWorldIndices.Clear();
		GatherColliders();
	}

	void PhysicsWorld::AppendMeshCollider(ColliderState& outState, const Collider& collider)
	{
		const ArrayList<Vector3>& localVertices = collider.GetMeshVertices();
		const ArrayList<uint32>& localIndices = collider.GetMeshIndices();
		if (localVertices.IsEmpty() || localIndices.IsEmpty())
		{
			return;
		}

		// The mesh is placed in the world ONCE per step, here, so the narrowphase
		// can test triangles without touching the scene graph again.
		Matrix4x4 worldMatrix;
		collider.GetOwnerWorldMatrix(worldMatrix.fElements);

		outState.uMeshVertexFirst = static_cast<uint32>(m_MeshWorldVertices.GetSize());
		outState.uMeshVertexCount = static_cast<uint32>(localVertices.GetSize());
		outState.uMeshIndexFirst = static_cast<uint32>(m_MeshWorldIndices.GetSize());
		outState.uMeshIndexCount = static_cast<uint32>(localIndices.GetSize());

		for (size_t nVertexIndex = 0; nVertexIndex < localVertices.GetSize(); ++nVertexIndex)
		{
			m_MeshWorldVertices.Add(worldMatrix.TransformPoint(localVertices[nVertexIndex]));
		}
		for (size_t nIndexIndex = 0; nIndexIndex < localIndices.GetSize(); ++nIndexIndex)
		{
			m_MeshWorldIndices.Add(localIndices[nIndexIndex]);
		}
	}

	uint32 PhysicsWorld::FindBodyIndex(NativeObjectHandle uGameObjectHandle, bool& bHasBody) const
	{
		for (size_t nBodyIndex = 0; nBodyIndex < m_BodyStates.GetSize(); ++nBodyIndex)
		{
			const Rigidbody* pRigidbody = Scene::Get().FindRigidbody(m_BodyStates[nBodyIndex].uRigidbodyHandle);
			if (pRigidbody != nullptr && pRigidbody->GetOwnerGameObjectHandle() == uGameObjectHandle)
			{
				bHasBody = true;
				return static_cast<uint32>(nBodyIndex);
			}
		}

		bHasBody = false;
		return k_nNoBodyIndex;
	}

	CollisionMesh PhysicsWorld::GetCollisionMesh(const ColliderState& state) const
	{
		CollisionMesh mesh;
		if (state.uMeshVertexCount == 0u || state.uMeshIndexCount == 0u)
		{
			return mesh;
		}

		mesh.pVertices = m_MeshWorldVertices.GetData() + state.uMeshVertexFirst;
		mesh.uVertexCount = state.uMeshVertexCount;
		mesh.pIndices = m_MeshWorldIndices.GetData() + state.uMeshIndexFirst;
		mesh.uIndexCount = state.uMeshIndexCount;
		return mesh;
	}

	// -------------------------------------------------------------------------
	// Contacts
	// -------------------------------------------------------------------------

	void PhysicsWorld::FindContacts()
	{
		const size_t nColliderCount = m_ColliderStates.GetSize();

		for (size_t nColliderIndexA = 0; nColliderIndexA < nColliderCount; ++nColliderIndexA)
		{
			const ColliderState& stateA = m_ColliderStates[nColliderIndexA];

			for (size_t nColliderIndexB = nColliderIndexA + 1; nColliderIndexB < nColliderCount; ++nColliderIndexB)
			{
				if (m_Stats.uPairCount >= k_nMaximumCollisionPairCount)
				{
					return;
				}

				const ColliderState& stateB = m_ColliderStates[nColliderIndexB];

				// Two bodies that cannot move have nothing to resolve, so the
				// narrowphase is not even asked.
				const bool bCanMoveA = stateA.bHasBody && m_BodyStates[stateA.uBodyIndex].fInverseMass > 0.0f;
				const bool bCanMoveB = stateB.bHasBody && m_BodyStates[stateB.uBodyIndex].fInverseMass > 0.0f;
				if (!bCanMoveA && !bCanMoveB)
				{
					continue;
				}

				if (!DoBoundsOverlap(stateA.BoundsMinimum, stateA.BoundsMaximum, stateB.BoundsMinimum, stateB.BoundsMaximum))
				{
					continue;
				}

				++m_Stats.uPairCount;

				const ColliderShape eShapeA = stateA.eShape;
				const ColliderShape eShapeB = stateB.eShape;

				// Two triangle soups have no narrowphase here: a mesh collider is
				// level geometry, and level geometry does not move into itself.
				if (eShapeA == ColliderShape::Mesh && eShapeB == ColliderShape::Mesh)
				{
					++m_Stats.uSkippedPairCount;
					continue;
				}

				PhysicsContact contact;
				bool bHasContact = false;
				bool bSwapNormal = false;

				if (eShapeA == ColliderShape::Box && eShapeB == ColliderShape::Box)
				{
					bHasContact = CollisionDetection::CollideBoxBox(stateA.Box, stateB.Box, contact);
				}
				else if (eShapeA == ColliderShape::Sphere && eShapeB == ColliderShape::Sphere)
				{
					bHasContact = CollisionDetection::CollideSphereSphere(stateA.Sphere, stateB.Sphere, contact);
				}
				else if (eShapeA == ColliderShape::Sphere && eShapeB == ColliderShape::Box)
				{
					bHasContact = CollisionDetection::CollideSphereBox(stateA.Sphere, stateB.Box, contact);
				}
				else if (eShapeA == ColliderShape::Box && eShapeB == ColliderShape::Sphere)
				{
					bHasContact = CollisionDetection::CollideSphereBox(stateB.Sphere, stateA.Box, contact);
					bSwapNormal = true;
				}
				else if (eShapeA == ColliderShape::Sphere && eShapeB == ColliderShape::Mesh)
				{
					bHasContact = CollisionDetection::CollideSphereMesh(stateA.Sphere, GetCollisionMesh(stateB), contact);
				}
				else if (eShapeA == ColliderShape::Mesh && eShapeB == ColliderShape::Sphere)
				{
					bHasContact = CollisionDetection::CollideSphereMesh(stateB.Sphere, GetCollisionMesh(stateA), contact);
					bSwapNormal = true;
				}
				else if (eShapeA == ColliderShape::Box && eShapeB == ColliderShape::Mesh)
				{
					bHasContact = CollisionDetection::CollideBoxMesh(stateA.Box, GetCollisionMesh(stateB), contact);
				}
				else if (eShapeA == ColliderShape::Mesh && eShapeB == ColliderShape::Box)
				{
					bHasContact = CollisionDetection::CollideBoxMesh(stateB.Box, GetCollisionMesh(stateA), contact);
					bSwapNormal = true;
				}

				if (!bHasContact)
				{
					continue;
				}

				// A swapped test answered "from B to A", so its normal is turned
				// back round before the handles are attached.
				if (bSwapNormal)
				{
					contact.Normal = -contact.Normal;
				}

				contact.uColliderHandleA = stateA.uColliderHandle;
				contact.uColliderHandleB = stateB.uColliderHandle;

				if (m_Contacts.GetSize() >= k_nMaximumContactCount)
				{
					return;
				}
				m_Contacts.Add(contact);
			}
		}
	}

	void PhysicsWorld::ResolveContacts()
	{
		m_Stats.uContactCount = static_cast<uint32>(m_Contacts.GetSize());
		if (m_Contacts.IsEmpty())
		{
			return;
		}

		// Sequential impulses: every pass walks the contacts in the same order and
		// each one sees the velocities the previous ones left behind, which is
		// what lets a stack of bodies settle instead of sinking into itself.
		for (uint32 uIteration = 0u; uIteration < m_uSolverIterationCount; ++uIteration)
		{
			for (size_t nContactIndex = 0; nContactIndex < m_Contacts.GetSize(); ++nContactIndex)
			{
				const PhysicsContact& contact = m_Contacts[nContactIndex];
				const ColliderState& stateA = FindColliderState(contact.uColliderHandleA);
				const ColliderState& stateB = FindColliderState(contact.uColliderHandleB);
				ApplyContactImpulse(contact, stateA, stateB);
			}
		}

		// The positional correction is a single pass: applying the same overlap
		// once per impulse pass would push a body out of the surface it rests on
		// several times over.
		for (size_t nContactIndex = 0; nContactIndex < m_Contacts.GetSize(); ++nContactIndex)
		{
			const PhysicsContact& contact = m_Contacts[nContactIndex];
			const ColliderState& stateA = FindColliderState(contact.uColliderHandleA);
			const ColliderState& stateB = FindColliderState(contact.uColliderHandleB);
			ApplyContactCorrection(contact, stateA, stateB);
		}
	}

	void PhysicsWorld::ApplyContactImpulse(const PhysicsContact& contact, const ColliderState& stateA, const ColliderState& stateB)
	{
		BodyState* pBodyA = (stateA.bHasBody ? &m_BodyStates[stateA.uBodyIndex] : nullptr);
		BodyState* pBodyB = (stateB.bHasBody ? &m_BodyStates[stateB.uBodyIndex] : nullptr);

		const float fInverseMassA = (pBodyA != nullptr) ? pBodyA->fInverseMass : 0.0f;
		const float fInverseMassB = (pBodyB != nullptr) ? pBodyB->fInverseMass : 0.0f;
		const float fInverseMassSum = fInverseMassA + fInverseMassB;
		if (fInverseMassSum <= 0.0f)
		{
			return;   // Neither side can move.
		}

		// The normal points from A to B, so a body moving towards the other one
		// closes the gap at a negative speed along it.
		const Vector3 velocityA = (pBodyA != nullptr) ? pBodyA->Velocity : Vector3::Zero;
		const Vector3 velocityB = (pBodyB != nullptr) ? pBodyB->Velocity : Vector3::Zero;
		const Vector3 relativeVelocity = velocityB - velocityA;
		const float fNormalSpeed = relativeVelocity.Dot(contact.Normal);
		if (fNormalSpeed >= 0.0f)
		{
			return;   // Already moving apart: the contact has been resolved.
		}

		// Bounce only above the threshold. A body that came to rest is pressed
		// into the ground by gravity every frame with a small speed, and giving
		// that speed back would leave it trembling forever.
		float fRestitution = 0.0f;
		if (-fNormalSpeed > Rigidbody::k_fBounceSpeedThreshold)
		{
			fRestitution = CombineRestitution(stateA.fRestitution, stateB.fRestitution);
		}

		const float fNormalImpulse = (-(1.0f + fRestitution) * fNormalSpeed) / fInverseMassSum;
		const Vector3 normalImpulseVector = contact.Normal * fNormalImpulse;

		if (pBodyA != nullptr && fInverseMassA > 0.0f)
		{
			pBodyA->Velocity -= normalImpulseVector * fInverseMassA;
		}
		if (pBodyB != nullptr && fInverseMassB > 0.0f)
		{
			pBodyB->Velocity += normalImpulseVector * fInverseMassB;
		}

		// Friction along the surface: the tangential part of the motion is
		// resisted by an impulse proportional to the normal one, capped by the
		// combined friction - Coulomb's law, in impulse form.
		const Vector3 tangentVelocity = relativeVelocity - (contact.Normal * fNormalSpeed);
		const float fTangentSpeed = tangentVelocity.GetLength();
		if (fTangentSpeed > k_fMinimumTangentialSpeed)
		{
			const Vector3 tangent = tangentVelocity / fTangentSpeed;
			float fFrictionImpulse = -fTangentSpeed / fInverseMassSum;

			const float fMaximumFrictionImpulse = fNormalImpulse * CombineFriction(stateA.fFriction, stateB.fFriction);
			if (fFrictionImpulse > fMaximumFrictionImpulse)
			{
				fFrictionImpulse = fMaximumFrictionImpulse;
			}
			else if (fFrictionImpulse < -fMaximumFrictionImpulse)
			{
				fFrictionImpulse = -fMaximumFrictionImpulse;
			}

			const Vector3 frictionImpulseVector = tangent * fFrictionImpulse;
			if (pBodyA != nullptr && fInverseMassA > 0.0f)
			{
				pBodyA->Velocity -= frictionImpulseVector * fInverseMassA;
			}
			if (pBodyB != nullptr && fInverseMassB > 0.0f)
			{
				pBodyB->Velocity += frictionImpulseVector * fInverseMassB;
			}
		}

		// A contact that actually pushed a sleeping body is what wakes it; a
		// resting contact (no approach speed) never does.
		const float fApproachSpeed = -fNormalSpeed;
		if (pBodyA != nullptr && pBodyA->bIsSleeping && pBodyA->fInverseMass > 0.0f && fApproachSpeed > k_fWakeSpeedThreshold)
		{
			pBodyA->bIsSleeping = false;
			pBodyA->fRestingSeconds = 0.0f;
		}
		if (pBodyB != nullptr && pBodyB->bIsSleeping && pBodyB->fInverseMass > 0.0f && fApproachSpeed > k_fWakeSpeedThreshold)
		{
			pBodyB->bIsSleeping = false;
			pBodyB->fRestingSeconds = 0.0f;
		}

		// "Standing on something" is a contact whose normal pushes the body up,
		// which is what a script asks about before it lets a character jump.
		if (contact.Normal.fY > k_fGroundedNormalThreshold && pBodyB != nullptr && pBodyB->fInverseMass > 0.0f)
		{
			pBodyB->bIsGrounded = true;
		}
		if (contact.Normal.fY < -k_fGroundedNormalThreshold && pBodyA != nullptr && pBodyA->fInverseMass > 0.0f)
		{
			pBodyA->bIsGrounded = true;
		}
	}

	void PhysicsWorld::ApplyContactCorrection(const PhysicsContact& contact, const ColliderState& stateA, const ColliderState& stateB)
	{
		if (contact.fPenetration <= 0.0f)
		{
			return;
		}

		BodyState* pBodyA = (stateA.bHasBody ? &m_BodyStates[stateA.uBodyIndex] : nullptr);
		BodyState* pBodyB = (stateB.bHasBody ? &m_BodyStates[stateB.uBodyIndex] : nullptr);

		const float fInverseMassA = (pBodyA != nullptr) ? pBodyA->fInverseMass : 0.0f;
		const float fInverseMassB = (pBodyB != nullptr) ? pBodyB->fInverseMass : 0.0f;
		const float fInverseMassSum = fInverseMassA + fInverseMassB;
		if (fInverseMassSum <= 0.0f)
		{
			return;
		}

		// Each body gives way in proportion to how easily it moves: a heavy body
		// moves a little, a light one a lot, and a static one not at all.
		const Vector3 correction = contact.Normal * contact.fPenetration;
		if (pBodyA != nullptr && fInverseMassA > 0.0f)
		{
			pBodyA->Position -= correction * (fInverseMassA / fInverseMassSum);
			WriteBodyTransform(*pBodyA);
		}
		if (pBodyB != nullptr && fInverseMassB > 0.0f)
		{
			pBodyB->Position += correction * (fInverseMassB / fInverseMassSum);
			WriteBodyTransform(*pBodyB);
		}
	}

	void PhysicsWorld::WriteBodyTransform(const BodyState& state)
	{
		Transform* pTransform = Scene::Get().FindTransform(state.uTransformHandle);
		if (pTransform == nullptr)
		{
			return;
		}

		pTransform->SetLocalPosition(state.Position.fX, state.Position.fY, state.Position.fZ);
	}

	void PhysicsWorld::FinishBodies(float fDeltaSeconds)
	{
		Scene& scene = Scene::Get();

		for (size_t nBodyIndex = 0; nBodyIndex < m_BodyStates.GetSize(); ++nBodyIndex)
		{
			BodyState& state = m_BodyStates[nBodyIndex];
			Rigidbody* pRigidbody = scene.FindRigidbody(state.uRigidbodyHandle);
			if (pRigidbody == nullptr)
			{
				continue;
			}

			// Sleeping: a body that stays slow for long enough stops being
			// integrated, which is what keeps a resting object perfectly still
			// instead of drifting on the noise of its own contacts.
			if (state.fInverseMass > 0.0f && !state.bIsSleeping)
			{
				const bool bIsSlow = (state.Velocity.GetLength() < Rigidbody::k_fSleepSpeedThreshold) &&
					(state.AngularVelocity.GetLength() < Rigidbody::k_fSleepAngularSpeedThreshold);
				if (bIsSlow && state.bIsGrounded)
				{
					state.fRestingSeconds += fDeltaSeconds;
				}
				else
				{
					state.fRestingSeconds = 0.0f;
				}

				if (state.fRestingSeconds >= Rigidbody::k_fSleepDelaySeconds)
				{
					state.Velocity = Vector3::Zero;
					state.AngularVelocity = Vector3::Zero;
					state.bIsSleeping = true;
				}
			}

			pRigidbody->ApplySimulationResult(
				state.Velocity,
				state.AngularVelocity,
				state.fRestingSeconds,
				state.bIsSleeping,
				state.bIsGrounded);
		}
	}

	const PhysicsWorld::ColliderState& PhysicsWorld::FindColliderState(NativeObjectHandle uColliderHandle) const
	{
		for (size_t nColliderIndex = 0; nColliderIndex < m_ColliderStates.GetSize(); ++nColliderIndex)
		{
			if (m_ColliderStates[nColliderIndex].uColliderHandle == uColliderHandle)
			{
				return m_ColliderStates[nColliderIndex];
			}
		}

		// A handle the step did not gather cannot be resolved; an empty state has
		// no body, so a solver that reaches here resolves nothing - which is the
		// right answer for a collider that was destroyed during the step.
		static const ColliderState s_EmptyState;
		return s_EmptyState;
	}

	// -------------------------------------------------------------------------
	// Queries
	// -------------------------------------------------------------------------

	bool PhysicsWorld::Raycast(
		const Vector3& Origin,
		const Vector3& Direction,
		float fMaximumDistance,
		PhysicsRaycastHit& outHit,
		NativeObjectHandle uIgnoredGameObjectHandle)
	{
		outHit = PhysicsRaycastHit();

		const float fDirectionLength = Direction.GetLength();
		if (fDirectionLength <= k_fMinimumDirectionLength || !(fMaximumDistance > 0.0f))
		{
			return false;
		}
		const Vector3 unitDirection = Direction / fDirectionLength;

		// A query answers about the scene as it is NOW: an object created, moved or
		// destroyed since the last step is part of the answer, which is what makes a
		// ray usable from anywhere - a script, a camera rig, a tool - and not only
		// from the code that just ran a step.
		RefreshColliderCache();

		bool bHasHit = false;
		float fNearestDistance = fMaximumDistance;

		for (size_t nColliderIndex = 0; nColliderIndex < m_ColliderStates.GetSize(); ++nColliderIndex)
		{
			const ColliderState& state = m_ColliderStates[nColliderIndex];
			if (uIgnoredGameObjectHandle != k_nInvalidObjectHandle &&
				state.uOwnerGameObjectHandle == uIgnoredGameObjectHandle)
			{
				continue;   // The object the caller follows is looked past.
			}

			PhysicsRaycastHit hit;
			bool bColliderHit = false;
			switch (state.eShape)
			{
			case ColliderShape::Sphere:
				bColliderHit = CollisionDetection::RaycastSphere(state.Sphere, Origin, unitDirection, fNearestDistance, hit);
				break;
			case ColliderShape::Mesh:
				bColliderHit = CollisionDetection::RaycastMesh(GetCollisionMesh(state), Origin, unitDirection, fNearestDistance, hit);
				break;
			case ColliderShape::Box:
			default:
				bColliderHit = CollisionDetection::RaycastBox(state.Box, Origin, unitDirection, fNearestDistance, hit);
				break;
			}

			if (!bColliderHit || hit.fDistance > fNearestDistance)
			{
				continue;
			}

			fNearestDistance = hit.fDistance;
			outHit = hit;
			outHit.bHasHit = true;
			outHit.uColliderHandle = state.uColliderHandle;
			bHasHit = true;
		}

		return bHasHit;
	}
}
