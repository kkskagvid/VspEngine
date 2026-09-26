#include "RuntimePCH.h"

#include "Classes/Collider.h"
#include "Classes/GameObject.h"
#include "Classes/Rigidbody.h"
#include "Classes/Scene.h"
#include "Physics/PhysicsWorld.h"
#include "Scripting/ScriptExport.h"

// -------------------------------------------------------------------------
// Physics exports consumed by managed code (C# -> C++).
// VspEngine.Rigidbody, VspEngine.Collider (and its shape subclasses) and
// VspEngine.Physics P/Invoke these exact names from VspCore.dll.
//
// A rigidbody and a collider belong to a game object, exactly like a camera;
// the shapes, the motion and the simulation live in Classes/Rigidbody,
// Classes/Collider and Physics/PhysicsWorld. Everything is plain data in and
// out - no exceptions cross the boundary, and an operation on a handle that
// does not resolve reports the empty value instead of failing.
// -------------------------------------------------------------------------

// -------- Rigidbody --------

CSHARP_EXPORT uint32 VspRigidbody_Create(uint32 uGameObjectHandle)
{
	return Vsp::Scene::Get().CreateRigidbody(uGameObjectHandle);
}

CSHARP_EXPORT int32 VspRigidbody_Destroy(uint32 uRigidbodyHandle)
{
	return Vsp::Scene::Get().DestroyRigidbody(uRigidbodyHandle) ? 1 : 0;
}

CSHARP_EXPORT uint32 VspRigidbody_GetGameObject(uint32 uRigidbodyHandle)
{
	const Vsp::Rigidbody* pRigidbody = Vsp::Scene::Get().FindRigidbody(uRigidbodyHandle);
	return pRigidbody != nullptr ? pRigidbody->GetOwnerGameObjectHandle() : Vsp::k_nInvalidObjectHandle;
}

CSHARP_EXPORT float VspRigidbody_GetMass(uint32 uRigidbodyHandle)
{
	const Vsp::Rigidbody* pRigidbody = Vsp::Scene::Get().FindRigidbody(uRigidbodyHandle);
	return pRigidbody != nullptr ? pRigidbody->GetMass() : 0.0f;
}

CSHARP_EXPORT void VspRigidbody_SetMass(uint32 uRigidbodyHandle, float fMass)
{
	Vsp::Rigidbody* pRigidbody = Vsp::Scene::Get().FindRigidbody(uRigidbodyHandle);
	if (pRigidbody != nullptr)
	{
		pRigidbody->SetMass(fMass);
	}
}

CSHARP_EXPORT float VspRigidbody_GetInverseMass(uint32 uRigidbodyHandle)
{
	const Vsp::Rigidbody* pRigidbody = Vsp::Scene::Get().FindRigidbody(uRigidbodyHandle);
	return pRigidbody != nullptr ? pRigidbody->GetInverseMass() : 0.0f;
}

CSHARP_EXPORT int32 VspRigidbody_IsKinematic(uint32 uRigidbodyHandle)
{
	const Vsp::Rigidbody* pRigidbody = Vsp::Scene::Get().FindRigidbody(uRigidbodyHandle);
	return (pRigidbody != nullptr && pRigidbody->IsKinematic()) ? 1 : 0;
}

CSHARP_EXPORT void VspRigidbody_SetKinematic(uint32 uRigidbodyHandle, int32 bIsKinematic)
{
	Vsp::Rigidbody* pRigidbody = Vsp::Scene::Get().FindRigidbody(uRigidbodyHandle);
	if (pRigidbody != nullptr)
	{
		pRigidbody->SetKinematic(bIsKinematic != 0);
	}
}

CSHARP_EXPORT int32 VspRigidbody_GetUseGravity(uint32 uRigidbodyHandle)
{
	const Vsp::Rigidbody* pRigidbody = Vsp::Scene::Get().FindRigidbody(uRigidbodyHandle);
	return (pRigidbody != nullptr && pRigidbody->GetUseGravity()) ? 1 : 0;
}

CSHARP_EXPORT void VspRigidbody_SetUseGravity(uint32 uRigidbodyHandle, int32 bUseGravity)
{
	Vsp::Rigidbody* pRigidbody = Vsp::Scene::Get().FindRigidbody(uRigidbodyHandle);
	if (pRigidbody != nullptr)
	{
		pRigidbody->SetUseGravity(bUseGravity != 0);
	}
}

CSHARP_EXPORT float VspRigidbody_GetGravityScale(uint32 uRigidbodyHandle)
{
	const Vsp::Rigidbody* pRigidbody = Vsp::Scene::Get().FindRigidbody(uRigidbodyHandle);
	return pRigidbody != nullptr ? pRigidbody->GetGravityScale() : 0.0f;
}

CSHARP_EXPORT void VspRigidbody_SetGravityScale(uint32 uRigidbodyHandle, float fGravityScale)
{
	Vsp::Rigidbody* pRigidbody = Vsp::Scene::Get().FindRigidbody(uRigidbodyHandle);
	if (pRigidbody != nullptr)
	{
		pRigidbody->SetGravityScale(fGravityScale);
	}
}

CSHARP_EXPORT void VspRigidbody_GetVelocity(uint32 uRigidbodyHandle, float* pOutXyz)
{
	if (pOutXyz == nullptr)
	{
		return;
	}

	const Vsp::Rigidbody* pRigidbody = Vsp::Scene::Get().FindRigidbody(uRigidbodyHandle);
	if (pRigidbody == nullptr)
	{
		pOutXyz[0] = 0.0f;
		pOutXyz[1] = 0.0f;
		pOutXyz[2] = 0.0f;
		return;
	}

	const Vsp::Vector3& velocity = pRigidbody->GetVelocity();
	pOutXyz[0] = velocity.fX;
	pOutXyz[1] = velocity.fY;
	pOutXyz[2] = velocity.fZ;
}

CSHARP_EXPORT void VspRigidbody_SetVelocity(uint32 uRigidbodyHandle, float fVelocityX, float fVelocityY, float fVelocityZ)
{
	Vsp::Rigidbody* pRigidbody = Vsp::Scene::Get().FindRigidbody(uRigidbodyHandle);
	if (pRigidbody != nullptr)
	{
		pRigidbody->SetVelocity(Vsp::Vector3(fVelocityX, fVelocityY, fVelocityZ));
	}
}

CSHARP_EXPORT void VspRigidbody_GetAngularVelocity(uint32 uRigidbodyHandle, float* pOutXyz)
{
	if (pOutXyz == nullptr)
	{
		return;
	}

	const Vsp::Rigidbody* pRigidbody = Vsp::Scene::Get().FindRigidbody(uRigidbodyHandle);
	if (pRigidbody == nullptr)
	{
		pOutXyz[0] = 0.0f;
		pOutXyz[1] = 0.0f;
		pOutXyz[2] = 0.0f;
		return;
	}

	const Vsp::Vector3& angularVelocity = pRigidbody->GetAngularVelocity();
	pOutXyz[0] = angularVelocity.fX;
	pOutXyz[1] = angularVelocity.fY;
	pOutXyz[2] = angularVelocity.fZ;
}

CSHARP_EXPORT void VspRigidbody_SetAngularVelocity(uint32 uRigidbodyHandle, float fVelocityX, float fVelocityY, float fVelocityZ)
{
	Vsp::Rigidbody* pRigidbody = Vsp::Scene::Get().FindRigidbody(uRigidbodyHandle);
	if (pRigidbody != nullptr)
	{
		pRigidbody->SetAngularVelocity(Vsp::Vector3(fVelocityX, fVelocityY, fVelocityZ));
	}
}

CSHARP_EXPORT int32 VspRigidbody_GetFreezeRotation(uint32 uRigidbodyHandle)
{
	const Vsp::Rigidbody* pRigidbody = Vsp::Scene::Get().FindRigidbody(uRigidbodyHandle);
	return (pRigidbody != nullptr && pRigidbody->GetFreezeRotation()) ? 1 : 0;
}

CSHARP_EXPORT void VspRigidbody_SetFreezeRotation(uint32 uRigidbodyHandle, int32 bFreezeRotation)
{
	Vsp::Rigidbody* pRigidbody = Vsp::Scene::Get().FindRigidbody(uRigidbodyHandle);
	if (pRigidbody != nullptr)
	{
		pRigidbody->SetFreezeRotation(bFreezeRotation != 0);
	}
}

CSHARP_EXPORT float VspRigidbody_GetLinearDrag(uint32 uRigidbodyHandle)
{
	const Vsp::Rigidbody* pRigidbody = Vsp::Scene::Get().FindRigidbody(uRigidbodyHandle);
	return pRigidbody != nullptr ? pRigidbody->GetLinearDrag() : 0.0f;
}

CSHARP_EXPORT void VspRigidbody_SetLinearDrag(uint32 uRigidbodyHandle, float fLinearDrag)
{
	Vsp::Rigidbody* pRigidbody = Vsp::Scene::Get().FindRigidbody(uRigidbodyHandle);
	if (pRigidbody != nullptr)
	{
		pRigidbody->SetLinearDrag(fLinearDrag);
	}
}

CSHARP_EXPORT float VspRigidbody_GetAngularDrag(uint32 uRigidbodyHandle)
{
	const Vsp::Rigidbody* pRigidbody = Vsp::Scene::Get().FindRigidbody(uRigidbodyHandle);
	return pRigidbody != nullptr ? pRigidbody->GetAngularDrag() : 0.0f;
}

CSHARP_EXPORT void VspRigidbody_SetAngularDrag(uint32 uRigidbodyHandle, float fAngularDrag)
{
	Vsp::Rigidbody* pRigidbody = Vsp::Scene::Get().FindRigidbody(uRigidbodyHandle);
	if (pRigidbody != nullptr)
	{
		pRigidbody->SetAngularDrag(fAngularDrag);
	}
}

CSHARP_EXPORT void VspRigidbody_AddForce(uint32 uRigidbodyHandle, float fForceX, float fForceY, float fForceZ)
{
	Vsp::Rigidbody* pRigidbody = Vsp::Scene::Get().FindRigidbody(uRigidbodyHandle);
	if (pRigidbody != nullptr)
	{
		pRigidbody->AddForce(Vsp::Vector3(fForceX, fForceY, fForceZ));
	}
}

CSHARP_EXPORT void VspRigidbody_AddImpulse(uint32 uRigidbodyHandle, float fImpulseX, float fImpulseY, float fImpulseZ)
{
	Vsp::Rigidbody* pRigidbody = Vsp::Scene::Get().FindRigidbody(uRigidbodyHandle);
	if (pRigidbody != nullptr)
	{
		pRigidbody->AddImpulse(Vsp::Vector3(fImpulseX, fImpulseY, fImpulseZ));
	}
}

CSHARP_EXPORT int32 VspRigidbody_IsSleeping(uint32 uRigidbodyHandle)
{
	const Vsp::Rigidbody* pRigidbody = Vsp::Scene::Get().FindRigidbody(uRigidbodyHandle);
	return (pRigidbody != nullptr && pRigidbody->IsSleeping()) ? 1 : 0;
}

CSHARP_EXPORT int32 VspRigidbody_IsGrounded(uint32 uRigidbodyHandle)
{
	const Vsp::Rigidbody* pRigidbody = Vsp::Scene::Get().FindRigidbody(uRigidbodyHandle);
	return (pRigidbody != nullptr && pRigidbody->IsGrounded()) ? 1 : 0;
}

CSHARP_EXPORT void VspRigidbody_Wake(uint32 uRigidbodyHandle)
{
	Vsp::Rigidbody* pRigidbody = Vsp::Scene::Get().FindRigidbody(uRigidbodyHandle);
	if (pRigidbody != nullptr)
	{
		pRigidbody->Wake();
	}
}

CSHARP_EXPORT void VspRigidbody_Sleep(uint32 uRigidbodyHandle)
{
	Vsp::Rigidbody* pRigidbody = Vsp::Scene::Get().FindRigidbody(uRigidbodyHandle);
	if (pRigidbody != nullptr)
	{
		pRigidbody->Sleep();
	}
}

CSHARP_EXPORT uint32 VspScene_GetRigidbodyHandle(uint32 uRigidbodyIndex)
{
	return Vsp::Scene::Get().GetLiveRigidbodyHandle(uRigidbodyIndex);
}

CSHARP_EXPORT uint32 VspScene_GetLiveRigidbodyCount()
{
	return Vsp::Scene::Get().GetLiveRigidbodyCount();
}

// -------- Collider --------

CSHARP_EXPORT uint32 VspCollider_Create(uint32 uGameObjectHandle)
{
	return Vsp::Scene::Get().CreateCollider(uGameObjectHandle);
}

CSHARP_EXPORT int32 VspCollider_Destroy(uint32 uColliderHandle)
{
	return Vsp::Scene::Get().DestroyCollider(uColliderHandle) ? 1 : 0;
}

CSHARP_EXPORT uint32 VspCollider_GetGameObject(uint32 uColliderHandle)
{
	const Vsp::Collider* pCollider = Vsp::Scene::Get().FindCollider(uColliderHandle);
	return pCollider != nullptr ? pCollider->GetOwnerGameObjectHandle() : Vsp::k_nInvalidObjectHandle;
}

CSHARP_EXPORT int32 VspCollider_GetShape(uint32 uColliderHandle)
{
	const Vsp::Collider* pCollider = Vsp::Scene::Get().FindCollider(uColliderHandle);
	return pCollider != nullptr ? static_cast<int32>(pCollider->GetShape()) : 0;
}

CSHARP_EXPORT void VspCollider_SetShape(uint32 uColliderHandle, int32 nShape)
{
	Vsp::Collider* pCollider = Vsp::Scene::Get().FindCollider(uColliderHandle);
	if (pCollider != nullptr)
	{
		pCollider->SetShape(static_cast<Vsp::ColliderShape>(nShape));
	}
}

CSHARP_EXPORT void VspCollider_GetBoxHalfExtents(uint32 uColliderHandle, float* pOutXyz)
{
	if (pOutXyz == nullptr)
	{
		return;
	}

	const Vsp::Collider* pCollider = Vsp::Scene::Get().FindCollider(uColliderHandle);
	if (pCollider == nullptr)
	{
		pOutXyz[0] = 0.0f;
		pOutXyz[1] = 0.0f;
		pOutXyz[2] = 0.0f;
		return;
	}

	const Vsp::Vector3& halfExtents = pCollider->GetBoxHalfExtents();
	pOutXyz[0] = halfExtents.fX;
	pOutXyz[1] = halfExtents.fY;
	pOutXyz[2] = halfExtents.fZ;
}

CSHARP_EXPORT void VspCollider_SetBoxHalfExtents(uint32 uColliderHandle, float fHalfExtentX, float fHalfExtentY, float fHalfExtentZ)
{
	Vsp::Collider* pCollider = Vsp::Scene::Get().FindCollider(uColliderHandle);
	if (pCollider != nullptr)
	{
		pCollider->SetBoxHalfExtents(Vsp::Vector3(fHalfExtentX, fHalfExtentY, fHalfExtentZ));
	}
}

CSHARP_EXPORT float VspCollider_GetSphereRadius(uint32 uColliderHandle)
{
	const Vsp::Collider* pCollider = Vsp::Scene::Get().FindCollider(uColliderHandle);
	return pCollider != nullptr ? pCollider->GetSphereRadius() : 0.0f;
}

CSHARP_EXPORT void VspCollider_SetSphereRadius(uint32 uColliderHandle, float fRadius)
{
	Vsp::Collider* pCollider = Vsp::Scene::Get().FindCollider(uColliderHandle);
	if (pCollider != nullptr)
	{
		pCollider->SetSphereRadius(fRadius);
	}
}

CSHARP_EXPORT int32 VspCollider_SetMesh(
	uint32 uColliderHandle,
	const float* pLocalPositionsXyz,
	uint32 uVertexCount,
	const uint32* pLocalIndices,
	uint32 uIndexCount)
{
	Vsp::Collider* pCollider = Vsp::Scene::Get().FindCollider(uColliderHandle);
	if (pCollider == nullptr)
	{
		return 0;
	}

	return pCollider->SetMesh(pLocalPositionsXyz, uVertexCount, pLocalIndices, uIndexCount) ? 1 : 0;
}

CSHARP_EXPORT uint32 VspCollider_GetMeshVertexCount(uint32 uColliderHandle)
{
	const Vsp::Collider* pCollider = Vsp::Scene::Get().FindCollider(uColliderHandle);
	return pCollider != nullptr ? pCollider->GetMeshVertexCount() : 0u;
}

CSHARP_EXPORT uint32 VspCollider_GetMeshTriangleCount(uint32 uColliderHandle)
{
	const Vsp::Collider* pCollider = Vsp::Scene::Get().FindCollider(uColliderHandle);
	return pCollider != nullptr ? pCollider->GetMeshTriangleCount() : 0u;
}

CSHARP_EXPORT void VspCollider_GetCenter(uint32 uColliderHandle, float* pOutXyz)
{
	if (pOutXyz == nullptr)
	{
		return;
	}

	const Vsp::Collider* pCollider = Vsp::Scene::Get().FindCollider(uColliderHandle);
	if (pCollider == nullptr)
	{
		pOutXyz[0] = 0.0f;
		pOutXyz[1] = 0.0f;
		pOutXyz[2] = 0.0f;
		return;
	}

	const Vsp::Vector3& center = pCollider->GetCenter();
	pOutXyz[0] = center.fX;
	pOutXyz[1] = center.fY;
	pOutXyz[2] = center.fZ;
}

CSHARP_EXPORT void VspCollider_SetCenter(uint32 uColliderHandle, float fCenterX, float fCenterY, float fCenterZ)
{
	Vsp::Collider* pCollider = Vsp::Scene::Get().FindCollider(uColliderHandle);
	if (pCollider != nullptr)
	{
		pCollider->SetCenter(Vsp::Vector3(fCenterX, fCenterY, fCenterZ));
	}
}

CSHARP_EXPORT float VspCollider_GetRestitution(uint32 uColliderHandle)
{
	const Vsp::Collider* pCollider = Vsp::Scene::Get().FindCollider(uColliderHandle);
	return pCollider != nullptr ? pCollider->GetRestitution() : 0.0f;
}

CSHARP_EXPORT void VspCollider_SetRestitution(uint32 uColliderHandle, float fRestitution)
{
	Vsp::Collider* pCollider = Vsp::Scene::Get().FindCollider(uColliderHandle);
	if (pCollider != nullptr)
	{
		pCollider->SetRestitution(fRestitution);
	}
}

CSHARP_EXPORT float VspCollider_GetFriction(uint32 uColliderHandle)
{
	const Vsp::Collider* pCollider = Vsp::Scene::Get().FindCollider(uColliderHandle);
	return pCollider != nullptr ? pCollider->GetFriction() : 0.0f;
}

CSHARP_EXPORT void VspCollider_SetFriction(uint32 uColliderHandle, float fFriction)
{
	Vsp::Collider* pCollider = Vsp::Scene::Get().FindCollider(uColliderHandle);
	if (pCollider != nullptr)
	{
		pCollider->SetFriction(fFriction);
	}
}

CSHARP_EXPORT void VspCollider_GetWorldCenter(uint32 uColliderHandle, float* pOutXyz)
{
	if (pOutXyz == nullptr)
	{
		return;
	}

	const Vsp::Collider* pCollider = Vsp::Scene::Get().FindCollider(uColliderHandle);
	if (pCollider == nullptr)
	{
		pOutXyz[0] = 0.0f;
		pOutXyz[1] = 0.0f;
		pOutXyz[2] = 0.0f;
		return;
	}

	// The shape's own centre, in the world: for a box or a sphere that is the
	// collider's centre, for a mesh the middle of its bounds.
	Vsp::Vector3 minimum;
	Vsp::Vector3 maximum;
	pCollider->GetWorldBounds(minimum, maximum);
	pOutXyz[0] = (minimum.fX + maximum.fX) * 0.5f;
	pOutXyz[1] = (minimum.fY + maximum.fY) * 0.5f;
	pOutXyz[2] = (minimum.fZ + maximum.fZ) * 0.5f;
}

CSHARP_EXPORT void VspCollider_GetWorldBounds(uint32 uColliderHandle, float* pOutMinimumXyz, float* pOutMaximumXyz)
{
	const Vsp::Collider* pCollider = Vsp::Scene::Get().FindCollider(uColliderHandle);
	if (pCollider == nullptr)
	{
		if (pOutMinimumXyz != nullptr)
		{
			pOutMinimumXyz[0] = 0.0f;
			pOutMinimumXyz[1] = 0.0f;
			pOutMinimumXyz[2] = 0.0f;
		}
		if (pOutMaximumXyz != nullptr)
		{
			pOutMaximumXyz[0] = 0.0f;
			pOutMaximumXyz[1] = 0.0f;
			pOutMaximumXyz[2] = 0.0f;
		}
		return;
	}

	Vsp::Vector3 minimum;
	Vsp::Vector3 maximum;
	pCollider->GetWorldBounds(minimum, maximum);

	if (pOutMinimumXyz != nullptr)
	{
		pOutMinimumXyz[0] = minimum.fX;
		pOutMinimumXyz[1] = minimum.fY;
		pOutMinimumXyz[2] = minimum.fZ;
	}
	if (pOutMaximumXyz != nullptr)
	{
		pOutMaximumXyz[0] = maximum.fX;
		pOutMaximumXyz[1] = maximum.fY;
		pOutMaximumXyz[2] = maximum.fZ;
	}
}

CSHARP_EXPORT uint32 VspScene_GetColliderHandle(uint32 uColliderIndex)
{
	return Vsp::Scene::Get().GetLiveColliderHandle(uColliderIndex);
}

CSHARP_EXPORT uint32 VspScene_GetLiveColliderCount()
{
	return Vsp::Scene::Get().GetLiveColliderCount();
}

// -------- The simulation --------

CSHARP_EXPORT void VspPhysics_Step(float fDeltaSeconds)
{
	Vsp::PhysicsWorld::Get().Step(fDeltaSeconds);
}

CSHARP_EXPORT void VspPhysics_GetGravity(float* pOutXyz)
{
	if (pOutXyz == nullptr)
	{
		return;
	}

	const Vsp::Vector3& gravity = Vsp::PhysicsWorld::Get().GetGravity();
	pOutXyz[0] = gravity.fX;
	pOutXyz[1] = gravity.fY;
	pOutXyz[2] = gravity.fZ;
}

CSHARP_EXPORT void VspPhysics_SetGravity(float fGravityX, float fGravityY, float fGravityZ)
{
	Vsp::PhysicsWorld::Get().SetGravity(Vsp::Vector3(fGravityX, fGravityY, fGravityZ));
}

CSHARP_EXPORT uint32 VspPhysics_GetSolverIterationCount()
{
	return Vsp::PhysicsWorld::Get().GetSolverIterationCount();
}

CSHARP_EXPORT void VspPhysics_SetSolverIterationCount(uint32 uSolverIterationCount)
{
	Vsp::PhysicsWorld::Get().SetSolverIterationCount(uSolverIterationCount);
}

CSHARP_EXPORT int32 VspPhysics_GetAutoSimulation()
{
	return Vsp::PhysicsWorld::Get().IsAutoSimulationEnabled() ? 1 : 0;
}

CSHARP_EXPORT void VspPhysics_SetAutoSimulation(int32 bAutoSimulationEnabled)
{
	Vsp::PhysicsWorld::Get().SetAutoSimulationEnabled(bAutoSimulationEnabled != 0);
}

CSHARP_EXPORT uint32 VspPhysics_GetContactCount()
{
	return Vsp::PhysicsWorld::Get().GetContactCount();
}

CSHARP_EXPORT void VspPhysics_GetContact(uint32 uContactIndex, uint32* pOutHandles2, float* pOutValues7)
{
	const Vsp::PhysicsWorld& world = Vsp::PhysicsWorld::Get();
	if (uContactIndex >= world.GetContactCount())
	{
		return;
	}

	const Vsp::PhysicsContact& contact = world.GetContact(uContactIndex);
	if (pOutHandles2 != nullptr)
	{
		pOutHandles2[0] = contact.uColliderHandleA;
		pOutHandles2[1] = contact.uColliderHandleB;
	}
	if (pOutValues7 != nullptr)
	{
		pOutValues7[0] = contact.Normal.fX;
		pOutValues7[1] = contact.Normal.fY;
		pOutValues7[2] = contact.Normal.fZ;
		pOutValues7[3] = contact.Point.fX;
		pOutValues7[4] = contact.Point.fY;
		pOutValues7[5] = contact.Point.fZ;
		pOutValues7[6] = contact.fPenetration;
	}
}

CSHARP_EXPORT void VspPhysics_GetStats(uint32* pOutValues6)
{
	if (pOutValues6 == nullptr)
	{
		return;
	}

	const Vsp::PhysicsStepStats& stats = Vsp::PhysicsWorld::Get().GetStats();
	pOutValues6[0] = stats.uColliderCount;
	pOutValues6[1] = stats.uDynamicBodyCount;
	pOutValues6[2] = stats.uPairCount;
	pOutValues6[3] = stats.uContactCount;
	pOutValues6[4] = stats.uSkippedPairCount;
	pOutValues6[5] = stats.uStepCount;
}

// Returns the collider the ray hit, or 0 when it hit nothing; pOutValues7 is
// filled with point (x, y, z), normal (x, y, z) and distance.
CSHARP_EXPORT uint32 VspPhysics_Raycast(
	float fOriginX, float fOriginY, float fOriginZ,
	float fDirectionX, float fDirectionY, float fDirectionZ,
	float fMaximumDistance,
	uint32 uIgnoredGameObjectHandle,
	float* pOutValues7)
{
	Vsp::PhysicsRaycastHit hit;
	const bool bHasHit = Vsp::PhysicsWorld::Get().Raycast(
		Vsp::Vector3(fOriginX, fOriginY, fOriginZ),
		Vsp::Vector3(fDirectionX, fDirectionY, fDirectionZ),
		fMaximumDistance,
		hit,
		uIgnoredGameObjectHandle);

	if (!bHasHit)
	{
		return Vsp::k_nInvalidObjectHandle;
	}

	if (pOutValues7 != nullptr)
	{
		pOutValues7[0] = hit.Point.fX;
		pOutValues7[1] = hit.Point.fY;
		pOutValues7[2] = hit.Point.fZ;
		pOutValues7[3] = hit.Normal.fX;
		pOutValues7[4] = hit.Normal.fY;
		pOutValues7[5] = hit.Normal.fZ;
		pOutValues7[6] = hit.fDistance;
	}
	return hit.uColliderHandle;
}
