#pragma once

#include "Classes/Object.h"
#include "Core/Core.h"
#include "Math/Vector3.h"

namespace Vsp
{
	// -------------------------------------------------------------------------
	// Physics - the plain values a simulation produces
	// -------------------------------------------------------------------------
	// Everything here is data in and data out: a contact, a ray hit and the
	// limits a step works within. No scene object, no backend and no platform
	// type appears in these structures, which is what lets the collision code be
	// exercised on its own.
	// -------------------------------------------------------------------------

	// -------------------------------------------------------------------------
	// ColliderShape
	// -------------------------------------------------------------------------
	// The volume a collider occupies. Every shape is described by the collider's
	// own data and placed by the transform of the game object it belongs to.
	//
	//   Box      a solid cuboid: half extents along the collider's local axes.
	//            The shape that fits a crate, a wall or the demo's cube.
	//   Sphere   a ball of one radius: the cheapest shape to test, and the one a
	//            character or a projectile is approximated with.
	//   Mesh     a triangle soup: the shape of real level geometry, which is
	//            never a box. Its triangles are given in the collider's local
	//            space, so an object that moves carries its mesh with it.
	// -------------------------------------------------------------------------
	enum class ColliderShape : uint32
	{
		Box = 0,
		Sphere = 1,
		Mesh = 2,
	};

	// -------------------------------------------------------------------------
	// PhysicsContact
	// -------------------------------------------------------------------------
	// One overlap the narrowphase found, in the form the solver consumes it:
	// WHO touched, HOW DEEP and in WHICH DIRECTION one has to move to stop
	// touching.
	//
	// The normal always points from collider A towards collider B, so the two
	// colliders of a pair are not interchangeable: A is the one the simulation
	// listed first, and pushing B along +normal (and A along -normal) separates
	// them.
	// -------------------------------------------------------------------------
	struct PhysicsContact
	{
		NativeObjectHandle uColliderHandleA = k_nInvalidObjectHandle;
		NativeObjectHandle uColliderHandleB = k_nInvalidObjectHandle;

		// Unit direction that separates B from A.
		Vector3 Normal = Vector3::Zero;

		// A point inside the overlap, in world space. The solver applies its
		// impulses through the centre of mass, so this is reported for the
		// caller's benefit (debug drawing, a game reading its contacts) rather
		// than used by the response.
		Vector3 Point = Vector3::Zero;

		// Overlap depth along the normal, in world units; always positive.
		float fPenetration = 0.0f;
	};

	// -------------------------------------------------------------------------
	// PhysicsRaycastHit
	// -------------------------------------------------------------------------
	// What a ray found: where it entered a collider, which way that surface
	// faces and how far along the ray the entry was.
	// -------------------------------------------------------------------------
	struct PhysicsRaycastHit
	{
		bool bHasHit = false;

		// Point the ray entered the collider at, in world space.
		Vector3 Point = Vector3::Zero;

		// Unit surface normal at that point, pointing out of the collider.
		Vector3 Normal = Vector3::Zero;

		// Distance along the (unit) ray direction, in world units.
		float fDistance = 0.0f;

		NativeObjectHandle uColliderHandle = k_nInvalidObjectHandle;
	};

	// -------------------------------------------------------------------------
	// Limits
	// -------------------------------------------------------------------------
	// One step produces at most this many contacts; a simulation that reaches
	// the limit reports it (GetStats) instead of growing without bound. 4096
	// contacts is far more than the scenes this engine renders produce.
	static constexpr uint32 k_nMaximumContactCount = 4096;

	// Collision pairs are tested at most this many times per step, so a scene
	// whose broadphase stops culling cannot stall a frame.
	static constexpr uint32 k_nMaximumCollisionPairCount = 16384;
}
