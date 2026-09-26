#include "RuntimePCH.h"

#include <cmath>

#include "Physics/CollisionDetection.h"

namespace Vsp
{
	namespace
	{
		// Length below which a direction counts as "no direction": a degenerate
		// triangle, two coincident centres, an axis that cancelled out.
		constexpr float k_fEpsilon = 1e-6f;

		// Largest overlap a test may report. An overlap of this size means the
		// shapes are effectively coincident, and clamping it keeps a solver from
		// teleporting a body that was placed exactly inside another one.
		constexpr float k_fMaximumPenetration = 1.0f;

		float ClampFloat(float fValue, float fMinimum, float fMaximum)
		{
			return (fValue < fMinimum) ? fMinimum : ((fValue > fMaximum) ? fMaximum : fValue);
		}

		float AbsoluteFloat(float fValue)
		{
			return (fValue < 0.0f) ? -fValue : fValue;
		}

		// A point of the box's own space placed in the world.
		Vector3 BoxLocalToWorld(const CollisionBox& Box, const Vector3& LocalPoint)
		{
			return Box.Center +
				(Box.AxisX * LocalPoint.fX) +
				(Box.AxisY * LocalPoint.fY) +
				(Box.AxisZ * LocalPoint.fZ);
		}

		// The same point expressed in the box's own space: each component is the
		// distance along one of the box's axes.
		Vector3 WorldToBoxLocal(const CollisionBox& Box, const Vector3& WorldPoint)
		{
			const Vector3 Offset = WorldPoint - Box.Center;
			return Vector3(Offset.Dot(Box.AxisX), Offset.Dot(Box.AxisY), Offset.Dot(Box.AxisZ));
		}

		// The point of a triangle closest to a query point. Ericson's method: the
		// triangle is divided into seven Voronoi regions - three vertices, three
		// edges and the face - and the region the query falls in answers it exactly,
		// where clamping a projection onto the triangle would only approximate it.
		Vector3 ClosestPointOnTriangle(const Vector3& Point, const Vector3& A, const Vector3& B, const Vector3& C)
		{
			const Vector3 EdgeAB = B - A;
			const Vector3 EdgeAC = C - A;

			// Vertex region of A.
			const Vector3 FromA = Point - A;
			const float fDotAB = EdgeAB.Dot(FromA);
			const float fDotAC = EdgeAC.Dot(FromA);
			if (fDotAB <= 0.0f && fDotAC <= 0.0f)
			{
				return A;
			}

			// Vertex region of B.
			const Vector3 FromB = Point - B;
			const float fDotBA = EdgeAB.Dot(FromB);
			const float fDotBC = EdgeAC.Dot(FromB);
			if (fDotBA >= 0.0f && fDotBC <= fDotBA)
			{
				return B;
			}

			// Edge region of AB: the barycentric weight of C, negated.
			const float fWeightC = (fDotAB * fDotBC) - (fDotBA * fDotAC);
			if (fWeightC <= 0.0f && fDotAB >= 0.0f && fDotBA <= 0.0f)
			{
				const float fDenominator = fDotAB - fDotBA;
				const float fFactor = (AbsoluteFloat(fDenominator) > k_fEpsilon) ? (fDotAB / fDenominator) : 0.0f;
				return A + (EdgeAB * fFactor);
			}

			// Vertex region of C.
			const Vector3 FromC = Point - C;
			const float fDotCA = EdgeAB.Dot(FromC);
			const float fDotCB = EdgeAC.Dot(FromC);
			if (fDotCB >= 0.0f && fDotCA <= fDotCB)
			{
				return C;
			}

			// Edge region of AC: the barycentric weight of B, negated.
			const float fWeightB = (fDotCA * fDotBC) - (fDotAB * fDotCB);
			if (fWeightB <= 0.0f && fDotCA >= 0.0f && fDotCB <= 0.0f)
			{
				const float fDenominator = fDotCA - fDotCB;
				const float fFactor = (AbsoluteFloat(fDenominator) > k_fEpsilon) ? (fDotCA / fDenominator) : 0.0f;
				return A + (EdgeAC * fFactor);
			}

			// Edge region of BC: the barycentric weight of A, negated.
			const float fWeightA = (fDotBA * fDotCB) - (fDotCA * fDotBC);
			if (fWeightA <= 0.0f && (fDotBC - fDotBA) >= 0.0f && (fDotCA - fDotCB) >= 0.0f)
			{
				const float fDenominator = (fDotBC - fDotBA) + (fDotCA - fDotCB);
				const float fFactor = (AbsoluteFloat(fDenominator) > k_fEpsilon)
					? ((fDotBC - fDotBA) / fDenominator)
					: 0.0f;
				return B + ((C - B) * fFactor);
			}

			// The face itself: the three weights normalized. Their sum measures the
			// triangle's area, so a degenerate triangle falls back to its first vertex.
			const float fWeightSum = fWeightA + fWeightB + fWeightC;
			if (AbsoluteFloat(fWeightSum) <= k_fEpsilon)
			{
				return A;
			}

			return A + (EdgeAB * (fWeightB / fWeightSum)) + (EdgeAC * (fWeightC / fWeightSum));
		}

		// Moller-Trumbore: the distance along the ray at which it crosses the
		// triangle, or a negative value when it misses. The triangle is treated as
		// two-sided, which is what a level mesh wants.
		float IntersectRayTriangle(
			const Vector3& Origin,
			const Vector3& Direction,
			const Vector3& Vertex0,
			const Vector3& Vertex1,
			const Vector3& Vertex2,
			Vector3& outTriangleNormal)
		{
			const Vector3 Edge1 = Vertex1 - Vertex0;
			const Vector3 Edge2 = Vertex2 - Vertex0;
			const Vector3 Normal = Vector3::Cross(Edge1, Edge2);
			if (Normal.GetLengthSquared() <= k_fEpsilon)
			{
				return -1.0f;   // Degenerate triangle: nothing to hit.
			}
			outTriangleNormal = Normal.GetNormalized();

			const Vector3 CrossDirection = Vector3::Cross(Direction, Edge2);
			const float fDeterminant = Edge1.Dot(CrossDirection);
			if (AbsoluteFloat(fDeterminant) <= k_fEpsilon)
			{
				return -1.0f;   // The ray runs parallel to the triangle's plane.
			}

			const float fInverseDeterminant = 1.0f / fDeterminant;
			const Vector3 FromVertex0 = Origin - Vertex0;

			const float fWeightU = FromVertex0.Dot(CrossDirection) * fInverseDeterminant;
			if (fWeightU < 0.0f || fWeightU > 1.0f)
			{
				return -1.0f;
			}

			const Vector3 CrossFromVertex0 = Vector3::Cross(FromVertex0, Edge1);
			const float fWeightV = Direction.Dot(CrossFromVertex0) * fInverseDeterminant;
			if (fWeightV < 0.0f || (fWeightU + fWeightV) > 1.0f)
			{
				return -1.0f;
			}

			return Edge2.Dot(CrossFromVertex0) * fInverseDeterminant;
		}
	}

	// -------------------------------------------------------------------------
	// CollisionBox
	// -------------------------------------------------------------------------

	Vector3 CollisionBox::GetAxis(uint32 uAxisIndex) const
	{
		if (uAxisIndex == 0)
		{
			return AxisX;
		}
		return (uAxisIndex == 1) ? AxisY : AxisZ;
	}

	Vector3 CollisionBox::GetCorner(uint32 uCornerIndex) const
	{
		const float fSignX = ((uCornerIndex & 1u) != 0u) ? 1.0f : -1.0f;
		const float fSignY = ((uCornerIndex & 2u) != 0u) ? 1.0f : -1.0f;
		const float fSignZ = ((uCornerIndex & 4u) != 0u) ? 1.0f : -1.0f;
		return BoxLocalToWorld(*this, Vector3(
			HalfExtents.fX * fSignX,
			HalfExtents.fY * fSignY,
			HalfExtents.fZ * fSignZ));
	}

	float CollisionBox::GetProjectionRadius(const Vector3& Axis) const
	{
		return (AbsoluteFloat(Axis.Dot(AxisX)) * HalfExtents.fX) +
			(AbsoluteFloat(Axis.Dot(AxisY)) * HalfExtents.fY) +
			(AbsoluteFloat(Axis.Dot(AxisZ)) * HalfExtents.fZ);
	}

	Vector3 CollisionBox::GetWorldPoint(const Vector3& LocalPoint) const
	{
		return BoxLocalToWorld(*this, LocalPoint);
	}

	float CollisionBox::GetVolume() const
	{
		return 8.0f * HalfExtents.fX * HalfExtents.fY * HalfExtents.fZ;
	}

	// -------------------------------------------------------------------------
	// CollisionMesh
	// -------------------------------------------------------------------------

	void CollisionMesh::GetTriangleVertices(
		uint32 uTriangleIndex,
		Vector3& outVertex0,
		Vector3& outVertex1,
		Vector3& outVertex2) const
	{
		outVertex0 = Vector3::Zero;
		outVertex1 = Vector3::Zero;
		outVertex2 = Vector3::Zero;

		if (pIndices == nullptr || pVertices == nullptr)
		{
			return;
		}

		const uint32 uFirstIndex = uTriangleIndex * 3u;
		if ((uFirstIndex + 2u) >= uIndexCount)
		{
			return;
		}

		// A malformed index list degenerates to the first vertex rather than
		// reading outside the array: a collider built from bad data must not be
		// able to crash a frame.
		const uint32 uIndex0 = (pIndices[uFirstIndex + 0u] < uVertexCount) ? pIndices[uFirstIndex + 0u] : 0u;
		const uint32 uIndex1 = (pIndices[uFirstIndex + 1u] < uVertexCount) ? pIndices[uFirstIndex + 1u] : 0u;
		const uint32 uIndex2 = (pIndices[uFirstIndex + 2u] < uVertexCount) ? pIndices[uFirstIndex + 2u] : 0u;

		outVertex0 = pVertices[uIndex0];
		outVertex1 = pVertices[uIndex1];
		outVertex2 = pVertices[uIndex2];
	}

	void CollisionMesh::GetBounds(Vector3& outMinimum, Vector3& outMaximum) const
	{
		outMinimum = Vector3::Zero;
		outMaximum = Vector3::Zero;

		if (pVertices == nullptr || uVertexCount == 0u)
		{
			return;
		}

		outMinimum = pVertices[0];
		outMaximum = pVertices[0];
		for (uint32 uVertexIndex = 1u; uVertexIndex < uVertexCount; ++uVertexIndex)
		{
			outMinimum = Vector3::Min(outMinimum, pVertices[uVertexIndex]);
			outMaximum = Vector3::Max(outMaximum, pVertices[uVertexIndex]);
		}
	}

	// -------------------------------------------------------------------------
	// Shape pairs
	// -------------------------------------------------------------------------

	bool CollisionDetection::CollideSphereSphere(
		const CollisionSphere& SphereA,
		const CollisionSphere& SphereB,
		PhysicsContact& outContact)
	{
		const Vector3 Offset = SphereB.Center - SphereA.Center;
		const float fRadiusSum = SphereA.fRadius + SphereB.fRadius;
		const float fDistanceSquared = Offset.GetLengthSquared();
		if (fDistanceSquared > (fRadiusSum * fRadiusSum))
		{
			return false;
		}

		const float fDistance = std::sqrt(fDistanceSquared);
		outContact.Normal = (fDistance > k_fEpsilon) ? (Offset / fDistance) : Vector3::UnitY;
		outContact.fPenetration = fRadiusSum - fDistance;
		outContact.Point = SphereA.Center + (outContact.Normal * (SphereA.fRadius - (outContact.fPenetration * 0.5f)));
		return true;
	}

	bool CollisionDetection::CollideSphereBox(
		const CollisionSphere& Sphere,
		const CollisionBox& Box,
		PhysicsContact& outContact)
	{
		// The sphere's centre in the box's own space, and the point of the box
		// closest to it: clamping each component to the half extents IS that
		// point, for a centre outside the box.
		const Vector3 LocalCenter = WorldToBoxLocal(Box, Sphere.Center);
		const Vector3 LocalClosest(
			ClampFloat(LocalCenter.fX, -Box.HalfExtents.fX, Box.HalfExtents.fX),
			ClampFloat(LocalCenter.fY, -Box.HalfExtents.fY, Box.HalfExtents.fY),
			ClampFloat(LocalCenter.fZ, -Box.HalfExtents.fZ, Box.HalfExtents.fZ));
		const Vector3 ClosestPoint = BoxLocalToWorld(Box, LocalClosest);

		// From the sphere towards the box, which is the pair convention.
		const Vector3 ToBox = ClosestPoint - Sphere.Center;
		const float fDistanceSquared = ToBox.GetLengthSquared();
		if (fDistanceSquared > (Sphere.fRadius * Sphere.fRadius))
		{
			return false;
		}

		if (fDistanceSquared > (k_fEpsilon * k_fEpsilon))
		{
			const float fDistance = std::sqrt(fDistanceSquared);
			outContact.Normal = ToBox / fDistance;
			outContact.fPenetration = Sphere.fRadius - fDistance;
			outContact.Point = ClosestPoint;
			return true;
		}

		// The centre is inside the box: the shortest way out is the face it is
		// nearest to, and the sphere must travel that far plus its own radius.
		const float fDistanceX = Box.HalfExtents.fX - AbsoluteFloat(LocalCenter.fX);
		const float fDistanceY = Box.HalfExtents.fY - AbsoluteFloat(LocalCenter.fY);
		const float fDistanceZ = Box.HalfExtents.fZ - AbsoluteFloat(LocalCenter.fZ);

		Vector3 OutwardAxis = Box.AxisX;
		float fOutwardDistance = fDistanceX;
		if (fDistanceY < fOutwardDistance)
		{
			OutwardAxis = Box.AxisY;
			fOutwardDistance = fDistanceY;
		}
		if (fDistanceZ < fOutwardDistance)
		{
			OutwardAxis = Box.AxisZ;
			fOutwardDistance = fDistanceZ;
		}
		if (LocalCenter.Dot(OutwardAxis) < 0.0f)
		{
			OutwardAxis = -OutwardAxis;
		}

		outContact.Normal = -OutwardAxis;   // The sphere leaves along -normal.
		outContact.fPenetration = Sphere.fRadius + fOutwardDistance;
		outContact.Point = Sphere.Center;
		return true;
	}

	bool CollisionDetection::CollideBoxBox(
		const CollisionBox& BoxA,
		const CollisionBox& BoxB,
		PhysicsContact& outContact)
	{
		// Separating-axis test: if any of the fifteen axes below separates the two
		// boxes, they do not touch. Otherwise the axis of LEAST overlap is the one
		// they have to be pushed apart along, and that overlap is the depth.
		const Vector3 BetweenCenters = BoxB.Center - BoxA.Center;

		Vector3 bestAxis = Vector3::UnitY;
		float fBestOverlap = k_fMaximumPenetration;

		// The three axes of A, the three of B, then the nine cross products of
		// one axis from each (which cover the edge-edge cases).
		for (uint32 uAxisIndex = 0u; uAxisIndex < 3u; ++uAxisIndex)
		{
			const Vector3 Axis = BoxA.GetAxis(uAxisIndex);
			const float fDistance = AbsoluteFloat(BetweenCenters.Dot(Axis));
			const float fOverlap = BoxA.GetProjectionRadius(Axis) + BoxB.GetProjectionRadius(Axis) - fDistance;
			if (fOverlap <= 0.0f)
			{
				return false;
			}
			if (fOverlap < fBestOverlap)
			{
				fBestOverlap = fOverlap;
				bestAxis = Axis;
			}
		}

		for (uint32 uAxisIndex = 0u; uAxisIndex < 3u; ++uAxisIndex)
		{
			const Vector3 Axis = BoxB.GetAxis(uAxisIndex);
			const float fDistance = AbsoluteFloat(BetweenCenters.Dot(Axis));
			const float fOverlap = BoxA.GetProjectionRadius(Axis) + BoxB.GetProjectionRadius(Axis) - fDistance;
			if (fOverlap <= 0.0f)
			{
				return false;
			}
			if (fOverlap < fBestOverlap)
			{
				fBestOverlap = fOverlap;
				bestAxis = Axis;
			}
		}

		for (uint32 uAxisA = 0u; uAxisA < 3u; ++uAxisA)
		{
			for (uint32 uAxisB = 0u; uAxisB < 3u; ++uAxisB)
			{
				const Vector3 Axis = Vector3::Cross(BoxA.GetAxis(uAxisA), BoxB.GetAxis(uAxisB));
				const float fAxisLength = Axis.GetLength();
				if (fAxisLength <= k_fEpsilon)
				{
					continue;   // Parallel edges: the cross product carries no axis.
				}

				const Vector3 UnitAxis = Axis / fAxisLength;
				const float fDistance = AbsoluteFloat(BetweenCenters.Dot(UnitAxis));
				const float fOverlap = BoxA.GetProjectionRadius(UnitAxis) + BoxB.GetProjectionRadius(UnitAxis) - fDistance;
				if (fOverlap <= 0.0f)
				{
					return false;
				}
				if (fOverlap < fBestOverlap)
				{
					fBestOverlap = fOverlap;
					bestAxis = UnitAxis;
				}
			}
		}

		// Orient the axis so it points from A towards B, and report the overlap
		// along it. The distance between the centres, measured along the axis,
		// decides the sign.
		const float fSignedDistance = BetweenCenters.Dot(bestAxis);
		outContact.Normal = (fSignedDistance < 0.0f) ? -bestAxis : bestAxis;
		outContact.fPenetration = fBestOverlap;

		// The middle of the overlap along the normal: the response is linear, so
		// the point is reported for the caller rather than used to torque a body.
		outContact.Point = BoxA.Center + (outContact.Normal * (fSignedDistance * 0.5f));
		return true;
	}

	bool CollisionDetection::CollideSphereMesh(
		const CollisionSphere& Sphere,
		const CollisionMesh& Mesh,
		PhysicsContact& outContact)
	{
		const uint32 uTriangleCount = Mesh.GetTriangleCount();
		float fDeepestDistance = Sphere.fRadius;
		bool bHasContact = false;

		for (uint32 uTriangleIndex = 0u; uTriangleIndex < uTriangleCount; ++uTriangleIndex)
		{
			Vector3 Vertex0;
			Vector3 Vertex1;
			Vector3 Vertex2;
			Mesh.GetTriangleVertices(uTriangleIndex, Vertex0, Vertex1, Vertex2);

			const Vector3 ClosestPoint = ClosestPointOnTriangle(Sphere.Center, Vertex0, Vertex1, Vertex2);
			const Vector3 ToMesh = ClosestPoint - Sphere.Center;
			const float fDistance = ToMesh.GetLength();

			if (fDistance > fDeepestDistance)
			{
				continue;
			}

			fDeepestDistance = fDistance;
			bHasContact = true;

			// From the sphere towards the mesh, which is the pair convention. A
			// centre exactly on the surface has no direction of its own, so the
			// triangle's own normal answers instead.
			if (fDistance > k_fEpsilon)
			{
				outContact.Normal = ToMesh / fDistance;
			}
			else
			{
				const Vector3 TriangleNormal = Vector3::Cross(Vertex1 - Vertex0, Vertex2 - Vertex0);
				outContact.Normal = (TriangleNormal.GetLengthSquared() > k_fEpsilon)
					? TriangleNormal.GetNormalized()
					: Vector3::UnitY;
			}

			outContact.fPenetration = Sphere.fRadius - fDistance;
			outContact.Point = ClosestPoint;
		}

		return bHasContact;
	}

	bool CollisionDetection::CollideBoxMesh(
		const CollisionBox& Box,
		const CollisionMesh& Mesh,
		PhysicsContact& outContact)
	{
		const uint32 uTriangleCount = Mesh.GetTriangleCount();
		PhysicsContact deepestContact;
		bool bHasContact = false;

		for (uint32 uTriangleIndex = 0u; uTriangleIndex < uTriangleCount; ++uTriangleIndex)
		{
			Vector3 Vertex0;
			Vector3 Vertex1;
			Vector3 Vertex2;
			Mesh.GetTriangleVertices(uTriangleIndex, Vertex0, Vertex1, Vertex2);

			PhysicsContact triangleContact;
			if (!CollideBoxTriangle(Box, Vertex0, Vertex1, Vertex2, triangleContact))
			{
				continue;
			}

			if (!bHasContact || triangleContact.fPenetration > deepestContact.fPenetration)
			{
				deepestContact = triangleContact;
				bHasContact = true;
			}
		}

		if (bHasContact)
		{
			outContact = deepestContact;
		}
		return bHasContact;
	}

	void CollisionDetection::SwapContact(PhysicsContact& contact)
	{
		const NativeObjectHandle uHandleA = contact.uColliderHandleA;
		contact.uColliderHandleA = contact.uColliderHandleB;
		contact.uColliderHandleB = uHandleA;
		contact.Normal = -contact.Normal;
	}
		// Box against one triangle.
		//
		// The separating-axis test decides WHETHER they touch: three box axes and the
		// nine cross products of a box axis with a triangle edge are enough to prove
		// two convex shapes apart. The DEPTH is then measured along the triangle's own
		// normal, because a triangle has no thickness: along a box axis its projection
		// is a flat interval that is either inside the box's slab or outside it, which
		// answers yes or no but never "how deep". The plane does answer that, and a
		// surface is exactly what a body resting on a mesh is held by.
		bool CollisionDetection::CollideBoxTriangle(
			const CollisionBox& Box,
			const Vector3& Vertex0,
			const Vector3& Vertex1,
			const Vector3& Vertex2,
			PhysicsContact& outContact)
		{
			// Work in the box's own space, where the box is an axis-aligned box centred
			// on the origin: the triangle's plane is then the only thing that has to be
			// described in the world again.
			const Vector3 LocalVertex0 = WorldToBoxLocal(Box, Vertex0);
			const Vector3 LocalVertex1 = WorldToBoxLocal(Box, Vertex1);
			const Vector3 LocalVertex2 = WorldToBoxLocal(Box, Vertex2);

			const Vector3 LocalEdge0 = LocalVertex1 - LocalVertex0;
			const Vector3 LocalEdge1 = LocalVertex2 - LocalVertex1;
			const Vector3 LocalEdge2 = LocalVertex0 - LocalVertex2;

			const Vector3 LocalAxes[3] = { Vector3::UnitX, Vector3::UnitY, Vector3::UnitZ };
			const Vector3 LocalEdges[3] = { LocalEdge0, LocalEdge1, LocalEdge2 };

			// One separating-axis test, answering only "apart or not". An axis of zero
			// length (parallel edges, a degenerate triangle) separates nothing.
			auto isSeparatedBy = [&](const Vector3& LocalAxis) -> bool
			{
				const float fAxisLengthSquared = LocalAxis.GetLengthSquared();
				if (fAxisLengthSquared <= k_fEpsilon)
				{
					return false;
				}

				const float fProjection0 = LocalAxis.Dot(LocalVertex0);
				const float fProjection1 = LocalAxis.Dot(LocalVertex1);
				const float fProjection2 = LocalAxis.Dot(LocalVertex2);
				const float fTriangleMinimum = (fProjection0 < fProjection1)
					? ((fProjection0 < fProjection2) ? fProjection0 : fProjection2)
					: ((fProjection1 < fProjection2) ? fProjection1 : fProjection2);
				const float fTriangleMaximum = (fProjection0 > fProjection1)
					? ((fProjection0 > fProjection2) ? fProjection0 : fProjection2)
					: ((fProjection1 > fProjection2) ? fProjection1 : fProjection2);

				const float fBoxRadius =
					(AbsoluteFloat(LocalAxis.fX) * Box.HalfExtents.fX) +
					(AbsoluteFloat(LocalAxis.fY) * Box.HalfExtents.fY) +
					(AbsoluteFloat(LocalAxis.fZ) * Box.HalfExtents.fZ);

				return (fTriangleMinimum > fBoxRadius) || (fTriangleMaximum < -fBoxRadius);
			};

			// The three axes of the box.
			for (uint32 uAxisIndex = 0u; uAxisIndex < 3u; ++uAxisIndex)
			{
				if (isSeparatedBy(LocalAxes[uAxisIndex]))
				{
					return false;
				}
			}

			// The nine cross products of a box axis with a triangle edge.
			for (uint32 uBoxAxisIndex = 0u; uBoxAxisIndex < 3u; ++uBoxAxisIndex)
			{
				for (uint32 uEdgeIndex = 0u; uEdgeIndex < 3u; ++uEdgeIndex)
				{
					if (isSeparatedBy(Vector3::Cross(LocalAxes[uBoxAxisIndex], LocalEdges[uEdgeIndex])))
					{
						return false;
					}
				}
			}

			// The triangle's own plane, which is where the depth comes from. A triangle
			// with no area is a line or a point and holds nothing up.
			const Vector3 LocalNormal = Vector3::Cross(LocalEdge0, LocalEdge1);
			const float fNormalLengthSquared = LocalNormal.GetLengthSquared();
			if (fNormalLengthSquared <= k_fEpsilon)
			{
				return false;
			}

			const float fNormalLength = std::sqrt(fNormalLengthSquared);
			const Vector3 UnitLocalNormal = LocalNormal / fNormalLength;

			// How far the box reaches along the normal, and how far its centre is from
			// the triangle's plane. The box is cut by the plane when the first is larger
			// than the second.
			const float fBoxRadiusAlongNormal =
				(AbsoluteFloat(UnitLocalNormal.fX) * Box.HalfExtents.fX) +
				(AbsoluteFloat(UnitLocalNormal.fY) * Box.HalfExtents.fY) +
				(AbsoluteFloat(UnitLocalNormal.fZ) * Box.HalfExtents.fZ);
			const float fPlaneDistance = UnitLocalNormal.Dot(LocalVertex0);
			if (AbsoluteFloat(fPlaneDistance) > fBoxRadiusAlongNormal)
			{
				return false;   // The whole box is on one side of the triangle's plane.
			}

			// The distance from the plane to the box's deepest point IS the overlap, and
			// it can never be more than the box is thick.
			float fPenetration = fBoxRadiusAlongNormal - AbsoluteFloat(fPlaneDistance);
			if (fPenetration < 0.0f)
			{
				fPenetration = 0.0f;
			}

			// Back to the world. The normal points from the BOX towards the TRIANGLE, so
			// the plane's own normal takes the side the box's centre is on.
			Vector3 worldNormal = (Box.AxisX * UnitLocalNormal.fX) +
				(Box.AxisY * UnitLocalNormal.fY) +
				(Box.AxisZ * UnitLocalNormal.fZ);
			worldNormal = (worldNormal.GetLengthSquared() > k_fEpsilon) ? worldNormal.GetNormalized() : Vector3::UnitY;
			if (fPlaneDistance < 0.0f)
			{
				worldNormal = -worldNormal;
			}

			outContact.Normal = worldNormal;
			outContact.fPenetration = fPenetration;
			outContact.Point = Box.Center + (worldNormal * (fBoxRadiusAlongNormal - (fPenetration * 0.5f)));
			return true;
		}

	// -------------------------------------------------------------------------
	// Rays
	// -------------------------------------------------------------------------

	bool CollisionDetection::RaycastSphere(
		const CollisionSphere& Sphere,
		const Vector3& Origin,
		const Vector3& Direction,
		float fMaximumDistance,
		PhysicsRaycastHit& outHit)
	{
		const Vector3 FromCenter = Origin - Sphere.Center;
		const float fHalfB = FromCenter.Dot(Direction);
		const float fConstant = FromCenter.GetLengthSquared() - (Sphere.fRadius * Sphere.fRadius);

		// The ray starts inside (or on) the sphere: it is already there.
		if (fConstant <= 0.0f)
		{
			outHit.bHasHit = true;
			outHit.fDistance = 0.0f;
			outHit.Point = Origin;
			outHit.Normal = (AbsoluteFloat(fHalfB) > k_fEpsilon) ? (FromCenter / fHalfB) : Vector3::UnitY;
			return true;
		}

		const float fDiscriminant = (fHalfB * fHalfB) - fConstant;
		if (fDiscriminant < 0.0f)
		{
			return false;
		}

		const float fDistance = -fHalfB - std::sqrt(fDiscriminant);
		if (fDistance < 0.0f || fDistance > fMaximumDistance)
		{
			return false;
		}

		outHit.bHasHit = true;
		outHit.fDistance = fDistance;
		outHit.Point = Origin + (Direction * fDistance);
		outHit.Normal = (outHit.Point - Sphere.Center).GetNormalized();
		return true;
	}

	bool CollisionDetection::RaycastBox(
		const CollisionBox& Box,
		const Vector3& Origin,
		const Vector3& Direction,
		float fMaximumDistance,
		PhysicsRaycastHit& outHit)
	{
		// Slab test in the box's own space: the ray crosses each pair of parallel
		// faces over an interval, and the entry is the last of the near hits.
		const Vector3 LocalOrigin = WorldToBoxLocal(Box, Origin);
		const Vector3 LocalDirection(
			Direction.Dot(Box.AxisX),
			Direction.Dot(Box.AxisY),
			Direction.Dot(Box.AxisZ));

		float fNearDistance = 0.0f;
		float fFarDistance = fMaximumDistance;
		Vector3 nearNormalLocal = Vector3::Zero;

		for (uint32 uAxisIndex = 0u; uAxisIndex < 3u; ++uAxisIndex)
		{
			const float fOrigin = LocalOrigin[uAxisIndex];
			const float fDirection = LocalDirection[uAxisIndex];
			const float fHalfExtent = Box.HalfExtents[uAxisIndex];

			if (AbsoluteFloat(fDirection) <= k_fEpsilon)
			{
				// Parallel to this pair of faces: the ray is either inside the
				// slab for its whole length or outside it for good.
				if (fOrigin < -fHalfExtent || fOrigin > fHalfExtent)
				{
					return false;
				}
				continue;
			}

			const float fInverseDirection = 1.0f / fDirection;
			float fFirstDistance = (-fHalfExtent - fOrigin) * fInverseDirection;
			float fSecondDistance = (fHalfExtent - fOrigin) * fInverseDirection;
			float fSideSign = -1.0f;
			if (fFirstDistance > fSecondDistance)
			{
				const float fSwap = fFirstDistance;
				fFirstDistance = fSecondDistance;
				fSecondDistance = fSwap;
				fSideSign = 1.0f;
			}

			if (fFirstDistance > fNearDistance)
			{
				fNearDistance = fFirstDistance;
				nearNormalLocal = Vector3::Zero;
				nearNormalLocal[uAxisIndex] = fSideSign;
			}
			if (fSecondDistance < fFarDistance)
			{
				fFarDistance = fSecondDistance;
			}
			if (fNearDistance > fFarDistance)
			{
				return false;
			}
		}

		if (fNearDistance > fMaximumDistance)
		{
			return false;
		}

		outHit.bHasHit = true;
		outHit.fDistance = fNearDistance;
		outHit.Point = Origin + (Direction * fNearDistance);

		// The local normal is one of the box's own axes, so it is placed in the
		// world the same way a point of the box's space is.
		const Vector3 WorldNormal = (Box.AxisX * nearNormalLocal.fX) +
			(Box.AxisY * nearNormalLocal.fY) +
			(Box.AxisZ * nearNormalLocal.fZ);
		outHit.Normal = (WorldNormal.GetLengthSquared() > k_fEpsilon) ? WorldNormal.GetNormalized() : -Direction;
		return true;
	}

	bool CollisionDetection::RaycastMesh(
		const CollisionMesh& Mesh,
		const Vector3& Origin,
		const Vector3& Direction,
		float fMaximumDistance,
		PhysicsRaycastHit& outHit)
	{
		const uint32 uTriangleCount = Mesh.GetTriangleCount();
		float fNearestDistance = fMaximumDistance;
		bool bHasHit = false;
		Vector3 nearestNormal = Vector3::Zero;

		for (uint32 uTriangleIndex = 0u; uTriangleIndex < uTriangleCount; ++uTriangleIndex)
		{
			Vector3 Vertex0;
			Vector3 Vertex1;
			Vector3 Vertex2;
			Mesh.GetTriangleVertices(uTriangleIndex, Vertex0, Vertex1, Vertex2);

			Vector3 triangleNormal;
			const float fDistance = IntersectRayTriangle(Origin, Direction, Vertex0, Vertex1, Vertex2, triangleNormal);
			if (fDistance < 0.0f || fDistance > fNearestDistance)
			{
				continue;
			}

			fNearestDistance = fDistance;
			// A triangle soup has no inside: the normal reported is the one that
			// faces the ray, so a caller can always offset along it.
			nearestNormal = (triangleNormal.Dot(Direction) > 0.0f) ? -triangleNormal : triangleNormal;
			bHasHit = true;
		}

		if (!bHasHit)
		{
			return false;
		}

		outHit.bHasHit = true;
		outHit.fDistance = fNearestDistance;
		outHit.Point = Origin + (Direction * fNearestDistance);
		outHit.Normal = nearestNormal;
		return true;
	}
}
