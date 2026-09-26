#pragma once

#include "Classes/Object.h"
#include "Core/Core.h"
#include "Core/Templates/ArrayList.h"
#include "Math/Matrix4x4.h"
#include "Math/Vector3.h"
#include "Physics/CollisionDetection.h"

namespace Vsp
{
	// -------------------------------------------------------------------------
	// Collider
	// -------------------------------------------------------------------------
	// The SHAPE of one object: the volume the physics simulation tests against
	// everything else, placed by the transform of the game object it belongs to.
	//
	// A collider describes its shape in its OWN local space - the half extents of
	// a box, the radius of a sphere, the triangles of a mesh - so moving or
	// turning the game object moves the shape with it and nothing has to be
	// rebuilt. PhysicsWorld turns that local description into the world-space
	// shapes the narrowphase works on, once per step.
	//
	// A collider is a `NativeObject` of its own kind, not a `Component`: like a
	// Camera, it is a piece of scene data a system reads, and the systems that
	// read it (PhysicsWorld, a raycast from a script) reach it through the
	// Scene's handle tables.
	//
	// The surface properties below are the two numbers a contact is resolved
	// with. They are per collider rather than per material because they describe
	// the SHAPE's behaviour, and a shape is what the solver sees.
	//
	// Nothing here throws: a shape setter clamps what it cannot use and a mesh
	// that cannot be stored leaves the collider as it was.
	// -------------------------------------------------------------------------
#pragma warning(push)
#pragma warning(disable : 4251)   // ArrayList members: header-only template.
	class RUNTIME_API Collider : public NativeObject
	{
	public:
		// Triangles one mesh collider may carry. A level mesh is built once and
		// tested triangle by triangle, so the limit is what keeps a runaway
		// asset from turning a frame into a minute.
		static constexpr uint32 k_nMaximumMeshTriangleCount = 65536;

		// -------- Owner --------
		// The game object whose transform places this shape.
		NativeObjectHandle GetOwnerGameObjectHandle() const { return m_uOwnerGameObjectHandle; }
		void SetOwnerGameObjectHandle(NativeObjectHandle uGameObjectHandle);

		// -------- Shape --------
		ColliderShape GetShape() const { return m_eShape; }
		void SetShape(ColliderShape eShape) { m_eShape = eShape; }

		// -------- Box --------
		// Half the size of the box along each of its own axes.
		const Vector3& GetBoxHalfExtents() const { return m_BoxHalfExtents; }
		void SetBoxHalfExtents(const Vector3& HalfExtents);

		// -------- Sphere --------
		float GetSphereRadius() const { return m_fSphereRadius; }
		void SetSphereRadius(float fSphereRadius);

		// -------- Mesh --------
		// Replaces the mesh with the given LOCAL-space positions (three floats per
		// vertex) and indices (three per triangle). Returns false - leaving the
		// previous mesh in place - when the data is unusable: no vertices, an
		// index list that is not a multiple of three, or a triangle count beyond
		// k_nMaximumMeshTriangleCount.
		bool SetMesh(const float* pLocalPositionsXyz, uint32 uVertexCount, const uint32* pLocalIndices, uint32 uIndexCount);

		// Empties the mesh, which makes a mesh collider collide with nothing.
		void ClearMesh();

		uint32 GetMeshVertexCount() const { return static_cast<uint32>(m_MeshVertices.GetSize()); }
		uint32 GetMeshTriangleCount() const { return static_cast<uint32>(m_MeshIndices.GetSize()) / 3u; }

		const ArrayList<Vector3>& GetMeshVertices() const { return m_MeshVertices; }
		const ArrayList<uint32>& GetMeshIndices() const { return m_MeshIndices; }

		// -------- Placement --------
		// Offset of the shape's centre from the object's origin, in the object's
		// local space. It is what lets one object carry a shape that is not
		// centred on it - a foot collider under a character, for instance.
		const Vector3& GetCenter() const { return m_Center; }
		void SetCenter(const Vector3& Center) { m_Center = Center; }

		// -------- Surface --------
		// How much of the approach speed a contact gives back: 0 stops the body
		// dead, 1 sends it back the way it came.
		float GetRestitution() const { return m_fRestitution; }
		void SetRestitution(float fRestitution);

		// How strongly a contact resists sliding along the surface: 0 is ice, 1
		// stops the tangential motion completely.
		float GetFriction() const { return m_fFriction; }
		void SetFriction(float fFriction);

		// -------- World shape --------
		// The shape placed in the world by the owner's transform. A shape read
		// before the collider is attached to a live object is placed at the
		// origin, so a caller always gets a usable value.
		CollisionBox GetWorldBox() const;
		CollisionSphere GetWorldSphere() const;

		// World-space bounding box of whatever shape this collider has, which is
		// what the broadphase compares.
		void GetWorldBounds(Vector3& outMinimum, Vector3& outMaximum) const;

		// The owner's world matrix, column-major, 16 floats.
		void GetOwnerWorldMatrix(float* pOutMatrix16) const;

	private:
		// The world matrix of the object that owns this collider; the identity
		// when there is none (a detached or destroyed object).
		Matrix4x4 ResolveOwnerWorldMatrix() const;

		NativeObjectHandle m_uOwnerGameObjectHandle = k_nInvalidObjectHandle;

		ColliderShape m_eShape = ColliderShape::Box;

		Vector3 m_BoxHalfExtents = Vector3(0.5f, 0.5f, 0.5f);
		float m_fSphereRadius = 0.5f;

		ArrayList<Vector3> m_MeshVertices;
		ArrayList<uint32> m_MeshIndices;

		Vector3 m_Center = Vector3::Zero;

		float m_fRestitution = 0.0f;
		float m_fFriction = 0.5f;
	};
#pragma warning(pop)
}
