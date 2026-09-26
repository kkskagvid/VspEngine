#include "RuntimePCH.h"

#include <cmath>
#include <utility>

#include "Classes/Collider.h"
#include "Classes/Scene.h"
#include "Classes/Transform.h"
#include "Core/Logging/Log.h"

namespace Vsp
{
	static constexpr const char* kLogTag = "Collider";

	namespace
	{
		// Length below which an axis of the world matrix counts as degenerate: a
		// zero scale would otherwise turn a normalized axis into a NaN.
		constexpr float k_fMinimumAxisLength = 1e-6f;

		// Turns one column of the world matrix into a unit axis. A column that
		// carries no length - the object's scale on that axis is zero - falls back
		// to the identity axis, so the shape stays finite.
		Vector3 NormalizeAxis(const Vector3& Axis, const Vector3& FallbackAxis, float& outScale)
		{
			const float fLength = Axis.GetLength();
			if (fLength <= k_fMinimumAxisLength)
			{
				outScale = 0.0f;
				return FallbackAxis;
			}

			outScale = fLength;
			return Axis / fLength;
		}
	}

	void Collider::SetOwnerGameObjectHandle(NativeObjectHandle uGameObjectHandle)
	{
		m_uOwnerGameObjectHandle = uGameObjectHandle;
	}

	void Collider::SetBoxHalfExtents(const Vector3& HalfExtents)
	{
		// A half extent is a distance: a negative one would turn the box inside
		// out, so it is held at zero or above.
		m_BoxHalfExtents = Vector3(
			(HalfExtents.fX > 0.0f) ? HalfExtents.fX : 0.0f,
			(HalfExtents.fY > 0.0f) ? HalfExtents.fY : 0.0f,
			(HalfExtents.fZ > 0.0f) ? HalfExtents.fZ : 0.0f);
	}

	void Collider::SetSphereRadius(float fSphereRadius)
	{
		m_fSphereRadius = (fSphereRadius > 0.0f) ? fSphereRadius : 0.0f;
	}

	bool Collider::SetMesh(const float* pLocalPositionsXyz, uint32 uVertexCount, const uint32* pLocalIndices, uint32 uIndexCount)
	{
		if (pLocalPositionsXyz == nullptr || uVertexCount == 0u || pLocalIndices == nullptr || uIndexCount == 0u)
		{
			LOG_ERROR(kLogTag, "A mesh collider needs vertices and indices; the mesh was left as it was.");
			return false;
		}

		if ((uIndexCount % 3u) != 0u)
		{
			LOG_ERROR(kLogTag, "A mesh collider's index count must be a multiple of three; the mesh was left as it was.");
			return false;
		}

		if ((uIndexCount / 3u) > k_nMaximumMeshTriangleCount)
		{
			LOG_ERROR(kLogTag, "A mesh collider holds at most {} triangles; the mesh was left as it was.",
				k_nMaximumMeshTriangleCount);
			return false;
		}

		// Every index has to address a vertex that exists: a list that does not is
		// refused as a whole rather than silently read out of bounds once a step.
		for (uint32 uIndexOffset = 0u; uIndexOffset < uIndexCount; ++uIndexOffset)
		{
			if (pLocalIndices[uIndexOffset] >= uVertexCount)
			{
				LOG_ERROR(kLogTag, "A mesh collider index {} addresses vertex {} of {}; the mesh was left as it was.",
					uIndexOffset, pLocalIndices[uIndexOffset], uVertexCount);
				return false;
			}
		}

		// Built into locals first, so a mesh that fails half way through cannot
		// leave the collider with a mixture of two meshes.
		ArrayList<Vector3> meshVertices;
		meshVertices.Reserve(uVertexCount);
		for (uint32 uVertexIndex = 0u; uVertexIndex < uVertexCount; ++uVertexIndex)
		{
			const uint32 uComponentOffset = uVertexIndex * 3u;
			meshVertices.Add(Vector3(
				pLocalPositionsXyz[uComponentOffset + 0u],
				pLocalPositionsXyz[uComponentOffset + 1u],
				pLocalPositionsXyz[uComponentOffset + 2u]));
		}

		ArrayList<uint32> meshIndices;
		meshIndices.Reserve(uIndexCount);
		for (uint32 uIndexOffset = 0u; uIndexOffset < uIndexCount; ++uIndexOffset)
		{
			meshIndices.Add(pLocalIndices[uIndexOffset]);
		}

		m_MeshVertices = std::move(meshVertices);
		m_MeshIndices = std::move(meshIndices);
		m_eShape = ColliderShape::Mesh;
		return true;
	}

	void Collider::ClearMesh()
	{
		m_MeshVertices.Clear();
		m_MeshIndices.Clear();
	}

	void Collider::SetRestitution(float fRestitution)
	{
		m_fRestitution = (fRestitution > 0.0f) ? ((fRestitution < 1.0f) ? fRestitution : 1.0f) : 0.0f;
	}

	void Collider::SetFriction(float fFriction)
	{
		m_fFriction = (fFriction > 0.0f) ? ((fFriction < 1.0f) ? fFriction : 1.0f) : 0.0f;
	}

	// -------------------------------------------------------------------------
	// The shape in the world
	// -------------------------------------------------------------------------

	Matrix4x4 Collider::ResolveOwnerWorldMatrix() const
	{
		Scene& scene = Scene::Get();
		Transform* pTransform = scene.FindTransform(scene.FindGameObjectTransformHandle(m_uOwnerGameObjectHandle));
		if (pTransform == nullptr)
		{
			return Matrix4x4::Identity();
		}

		Matrix4x4 worldMatrix;
		pTransform->GetWorldMatrix(worldMatrix.fElements);
		return worldMatrix;
	}

	void Collider::GetOwnerWorldMatrix(float* pOutMatrix16) const
	{
		if (pOutMatrix16 == nullptr)
		{
			return;
		}

		const Matrix4x4 worldMatrix = ResolveOwnerWorldMatrix();
		for (uint32 uElementIndex = 0u; uElementIndex < 16u; ++uElementIndex)
		{
			pOutMatrix16[uElementIndex] = worldMatrix.fElements[uElementIndex];
		}
	}

	CollisionBox Collider::GetWorldBox() const
	{
		const Matrix4x4 worldMatrix = ResolveOwnerWorldMatrix();

		// The columns of the world matrix are the object's own axes, stretched by
		// its scale; normalizing them gives the box's orientation and the lengths
		// give the scale the half extents are multiplied by.
		float fScaleX = 1.0f;
		float fScaleY = 1.0f;
		float fScaleZ = 1.0f;

		CollisionBox box;
		box.AxisX = NormalizeAxis(worldMatrix.GetColumn(0), Vector3::UnitX, fScaleX);
		box.AxisY = NormalizeAxis(worldMatrix.GetColumn(1), Vector3::UnitY, fScaleY);
		box.AxisZ = NormalizeAxis(worldMatrix.GetColumn(2), Vector3::UnitZ, fScaleZ);
		box.Center = worldMatrix.TransformPoint(m_Center);
		box.HalfExtents = Vector3(
			m_BoxHalfExtents.fX * fScaleX,
			m_BoxHalfExtents.fY * fScaleY,
			m_BoxHalfExtents.fZ * fScaleZ);
		return box;
	}

	CollisionSphere Collider::GetWorldSphere() const
	{
		const Matrix4x4 worldMatrix = ResolveOwnerWorldMatrix();

		// A sphere has one radius, so the largest scale of the object is the one
		// it follows: a shape that is inside the object's scaled bounds is what a
		// collision test wants, never one that is larger on one axis only.
		float fScaleX = 1.0f;
		float fScaleY = 1.0f;
		float fScaleZ = 1.0f;
		NormalizeAxis(worldMatrix.GetColumn(0), Vector3::UnitX, fScaleX);
		NormalizeAxis(worldMatrix.GetColumn(1), Vector3::UnitY, fScaleY);
		NormalizeAxis(worldMatrix.GetColumn(2), Vector3::UnitZ, fScaleZ);

		float fLargestScale = fScaleX;
		if (fScaleY > fLargestScale)
		{
			fLargestScale = fScaleY;
		}
		if (fScaleZ > fLargestScale)
		{
			fLargestScale = fScaleZ;
		}

		CollisionSphere sphere;
		sphere.Center = worldMatrix.TransformPoint(m_Center);
		sphere.fRadius = m_fSphereRadius * fLargestScale;
		return sphere;
	}

	void Collider::GetWorldBounds(Vector3& outMinimum, Vector3& outMaximum) const
	{
		outMinimum = Vector3::Zero;
		outMaximum = Vector3::Zero;

		switch (m_eShape)
		{
		case ColliderShape::Sphere:
		{
			const CollisionSphere sphere = GetWorldSphere();
			outMinimum = sphere.Center - Vector3(sphere.fRadius, sphere.fRadius, sphere.fRadius);
			outMaximum = sphere.Center + Vector3(sphere.fRadius, sphere.fRadius, sphere.fRadius);
			return;
		}

		case ColliderShape::Mesh:
		{
			if (m_MeshVertices.IsEmpty())
			{
				return;
			}

			const Matrix4x4 worldMatrix = ResolveOwnerWorldMatrix();
			outMinimum = worldMatrix.TransformPoint(m_MeshVertices[0]);
			outMaximum = outMinimum;
			for (size_t nVertexIndex = 1; nVertexIndex < m_MeshVertices.GetSize(); ++nVertexIndex)
			{
				const Vector3 worldVertex = worldMatrix.TransformPoint(m_MeshVertices[nVertexIndex]);
				outMinimum = Vector3::Min(outMinimum, worldVertex);
				outMaximum = Vector3::Max(outMaximum, worldVertex);
			}
			return;
		}

		case ColliderShape::Box:
		default:
		{
			// The support radius of the box along each world axis IS half its
			// extent there, which is cheaper than transforming eight corners.
			const CollisionBox box = GetWorldBox();
			const Vector3 extents(
				box.GetProjectionRadius(Vector3::UnitX),
				box.GetProjectionRadius(Vector3::UnitY),
				box.GetProjectionRadius(Vector3::UnitZ));
			outMinimum = box.Center - extents;
			outMaximum = box.Center + extents;
			return;
		}
		}
	}
}
