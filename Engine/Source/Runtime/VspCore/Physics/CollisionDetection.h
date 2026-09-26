#pragma once

#include "Core/Core.h"
#include "Math/Vector3.h"
#include "Physics/PhysicsTypes.h"

namespace Vsp
{
	// -------------------------------------------------------------------------
	// CollisionDetection
	// -------------------------------------------------------------------------
	// The narrowphase: given two shapes, does one overlap the other, and how
	// would they have to move to stop overlapping.
	//
	// Nothing here knows about the scene, the renderer or the platform. A shape
	// is a handful of vectors in WORLD space, produced by Classes/Collider from
	// the transform of the object it belongs to, and every function is a plain
	// calculation that allocates nothing and throws nothing. That is what makes
	// the narrowphase testable on its own and identical on every platform.
	//
	// The pair convention is the one PhysicsContact documents: a function fills a
	// contact whose normal points from its FIRST shape towards its SECOND, so a
	// caller that has the shapes the other way round swaps the result
	// (CollisionDetection::SwapContact).
	// -------------------------------------------------------------------------

	// -------------------------------------------------------------------------
	// CollisionBox
	// -------------------------------------------------------------------------
	// An oriented box: a world centre, three ORTHONORMAL axes and the half extent
	// along each of them. Storing the axes rather than a matrix keeps the
	// separating-axis tests free of any convention: an axis IS the direction it
	// is asked about.
	struct CollisionBox
	{
		Vector3 Center = Vector3::Zero;
		Vector3 AxisX = Vector3::UnitX;
		Vector3 AxisY = Vector3::UnitY;
		Vector3 AxisZ = Vector3::UnitZ;
		Vector3 HalfExtents = Vector3::Zero;

		// The axis with the given index (0 = X, 1 = Y, 2 = Z).
		Vector3 GetAxis(uint32 uAxisIndex) const;

		// One of the eight corners: the sign bits of uCornerIndex pick the side
		// along each axis (bit 0 = X, bit 1 = Y, bit 2 = Z).
		Vector3 GetCorner(uint32 uCornerIndex) const;

		// How far the box reaches along an arbitrary axis: the support radius of
		// the box, which is what a separating-axis test compares distances with.
		float GetProjectionRadius(const Vector3& Axis) const;

		// A point of the box's own space (each component in [-1, 1] at the box's
		// surface) placed in the world.
		Vector3 GetWorldPoint(const Vector3& LocalPoint) const;

		// Volume in cubic world units; a measure of how big the shape is, used to
		// order a pair so the smaller shape is the one that moves.
		float GetVolume() const;
	};

	// -------------------------------------------------------------------------
	// CollisionSphere
	// -------------------------------------------------------------------------
	struct CollisionSphere
	{
		Vector3 Center = Vector3::Zero;
		float fRadius = 0.0f;
	};

	// -------------------------------------------------------------------------
	// CollisionMesh
	// -------------------------------------------------------------------------
	// A triangle soup in WORLD space. It borrows the arrays it is built from, so
	// a caller that already holds world-space vertices (PhysicsWorld does, once
	// per step) pays no copy to hand them to the narrowphase.
	struct CollisionMesh
	{
		const Vector3* pVertices = nullptr;
		uint32 uVertexCount = 0;
		const uint32* pIndices = nullptr;
		uint32 uIndexCount = 0;

		// Number of whole triangles the index list describes.
		uint32 GetTriangleCount() const { return uIndexCount / 3u; }

		// The three vertices of one triangle; the indices are clamped into range,
		// so a malformed list degenerates instead of reading out of bounds.
		void GetTriangleVertices(uint32 uTriangleIndex, Vector3& outVertex0, Vector3& outVertex1, Vector3& outVertex2) const;

		// Bounding box of every vertex; both outputs stay at zero for an empty
		// mesh, which a caller reads as "no bounds".
		void GetBounds(Vector3& outMinimum, Vector3& outMaximum) const;
	};

	class RUNTIME_API CollisionDetection
	{
	public:
		// -------- Shape pairs --------
		// Each function returns true when the shapes overlap and then fills
		// outContact with the normal (from the first shape towards the second),
		// a point inside the overlap and the depth along that normal.

		static bool CollideSphereSphere(const CollisionSphere& SphereA, const CollisionSphere& SphereB, PhysicsContact& outContact);
		static bool CollideSphereBox(const CollisionSphere& Sphere, const CollisionBox& Box, PhysicsContact& outContact);
		static bool CollideBoxBox(const CollisionBox& BoxA, const CollisionBox& BoxB, PhysicsContact& outContact);
		static bool CollideSphereMesh(const CollisionSphere& Sphere, const CollisionMesh& Mesh, PhysicsContact& outContact);
		static bool CollideBoxMesh(const CollisionBox& Box, const CollisionMesh& Mesh, PhysicsContact& outContact);

		// Turns a contact of (A, B) into one of (B, A): the normal flips and the
		// two collider handles change places.
		static void SwapContact(PhysicsContact& contact);

		// -------- Rays --------
		// A ray is an origin and a UNIT direction; fMaximumDistance bounds it. The
		// functions return true when the ray enters the shape within that range.

		static bool RaycastSphere(const CollisionSphere& Sphere, const Vector3& Origin, const Vector3& Direction, float fMaximumDistance, PhysicsRaycastHit& outHit);
		static bool RaycastBox(const CollisionBox& Box, const Vector3& Origin, const Vector3& Direction, float fMaximumDistance, PhysicsRaycastHit& outHit);
		static bool RaycastMesh(const CollisionMesh& Mesh, const Vector3& Origin, const Vector3& Direction, float fMaximumDistance, PhysicsRaycastHit& outHit);

	private:
		// Box against one triangle, using the separating-axis test: three box
		// axes, the triangle's normal and the nine cross products of the box axes
		// with the triangle's edges. Returns the overlap along the axis of least
		// separation, which is the direction the box leaves the triangle by.
		static bool CollideBoxTriangle(const CollisionBox& Box, const Vector3& Vertex0, const Vector3& Vertex1, const Vector3& Vertex2, PhysicsContact& outContact);
	};
}
