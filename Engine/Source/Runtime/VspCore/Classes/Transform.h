#pragma once

#include "Classes/Object.h"
#include "Core/Core.h"
#include "Core/Templates/ArrayList.h"

namespace Vsp
{
	// -------------------------------------------------------------------------
	// Transform
	// -------------------------------------------------------------------------
	// Native storage of one object's place in the scene graph. A transform keeps
	// its LOCAL position, rotation (Euler degrees) and scale - the values a
	// script writes - and derives the WORLD transform from its parent chain,
	// which is what rendering and the camera read.
	//
	// Managed code reaches it through the VspEngine.Transform reference handle,
	// which owns nothing but the handle the Scene hands out; the world values,
	// the parent links and the local values all live here.
	//
	// Matrices are column-major 4x4 float arrays, the layout glm produces and the
	// shaders expect (a "column_major float4x4" block member reads them directly).
	//
	// A transform always belongs to exactly one GameObject (its owner); the owner
	// handle is 0 while the transform is being created.
	// -------------------------------------------------------------------------
#pragma warning(push)
#pragma warning(disable : 4251)   // ArrayList member: header-only template.
	class RUNTIME_API Transform : public NativeObject
	{
	public:
		// -------- Owner --------
		NativeObjectHandle GetOwnerGameObjectHandle() const { return m_uOwnerGameObjectHandle; }
		void SetOwnerGameObjectHandle(NativeObjectHandle uGameObjectHandle);

		// -------- Local position --------
		void GetLocalPosition(float& outPositionX, float& outPositionY, float& outPositionZ) const;
		void SetLocalPosition(float fPositionX, float fPositionY, float fPositionZ);

		// -------- Local rotation (Euler angles, degrees, applied Z then Y then X) --------
		void GetLocalRotation(float& outRotationX, float& outRotationY, float& outRotationZ) const;
		void SetLocalRotation(float fRotationX, float fRotationY, float fRotationZ);

		// -------- Local scale --------
		void GetLocalScale(float& outScaleX, float& outScaleY, float& outScaleZ) const;
		void SetLocalScale(float fScaleX, float fScaleY, float fScaleZ);

		// -------- World transform (derived from the parent chain) --------
		// Reading a world value refreshes the chain it depends on, so a caller
		// always sees what the scene currently is.
		void GetWorldPosition(float& outPositionX, float& outPositionY, float& outPositionZ);
		void GetWorldRotation(float& outRotationX, float& outRotationY, float& outRotationZ);
		void GetWorldScale(float& outScaleX, float& outScaleY, float& outScaleZ);

		// Moves the object to a world position, keeping its local rotation and
		// scale: the local position becomes the one that lands there through the
		// parent chain.
		void SetWorldPosition(float fPositionX, float fPositionY, float fPositionZ);

		// -------- Matrices (column-major, 16 floats each) --------
		// The local transform: translation * rotation * scale.
		void GetLocalMatrix(float* pOutMatrix) const;

		// The transform in world space: parent world * local.
		void GetWorldMatrix(float* pOutMatrix);

		// The inverse of the world matrix, which takes a world point into this
		// object's local space.
		void GetWorldToLocalMatrix(float* pOutMatrix);

		// -------- Hierarchy --------
		// The parent transform, or 0 when the transform is a scene root.
		NativeObjectHandle GetParentTransformHandle() const { return m_uParentTransformHandle; }

		// Re-parents the transform, keeping its LOCAL values (a caller that wants
		// to keep the world position adjusts it afterwards, which is what the
		// managed Transform.SetParent(parent, keepWorldPosition) does).
		// Returns false when the handle is not a live transform or when it would
		// create a cycle (a transform cannot become its own descendant).
		bool SetParentTransformHandle(NativeObjectHandle uParentTransformHandle);

		uint32 GetChildTransformCount() const { return static_cast<uint32>(m_ChildTransformHandles.GetSize()); }
		NativeObjectHandle GetChildTransformHandle(uint32 uChildIndex) const;

		// Removes the transform from its parent's child list; called when the
		// Scene releases it.
		void DetachFromParent();

		// -------- Change tracking --------
		// Every setter raises the dirty flag; consumers (e.g. a render pipeline
		// building its per-frame draw list) clear it once they picked the value
		// up.
		bool IsDirty() const { return m_bIsDirty; }
		void ClearDirtyFlag() { m_bIsDirty = false; }

	private:
		// Marks this transform and everything below it as needing a new world
		// transform.
		void MarkWorldDirty();

		// Recomputes this transform's world matrix when it is stale.
		void EnsureWorldMatrix();

		// The live parent, or nullptr when there is none (a released parent
		// counts as none).
		Transform* ResolveParent() const;

		// Builds translation * rotation * scale into a column-major matrix.
		void BuildLocalMatrix(float* pOutMatrix) const;

		NativeObjectHandle m_uOwnerGameObjectHandle = k_nInvalidObjectHandle;
		NativeObjectHandle m_uParentTransformHandle = k_nInvalidObjectHandle;
		ArrayList<NativeObjectHandle> m_ChildTransformHandles;

		float m_fLocalPositionX = 0.0f;
		float m_fLocalPositionY = 0.0f;
		float m_fLocalPositionZ = 0.0f;

		float m_fLocalRotationX = 0.0f;
		float m_fLocalRotationY = 0.0f;
		float m_fLocalRotationZ = 0.0f;

		float m_fLocalScaleX = 1.0f;
		float m_fLocalScaleY = 1.0f;
		float m_fLocalScaleZ = 1.0f;

		// World matrix of this transform, valid while m_bWorldIsDirty is false.
		float m_WorldMatrix[16] = {};
		bool m_bWorldIsDirty = true;

		bool m_bIsDirty = true;
	};
#pragma warning(pop)
}
