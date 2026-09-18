#pragma once

#include "Classes/Object.h"
#include "Core/Core.h"

namespace Vsp
{
	// -------------------------------------------------------------------------
	// Transform
	// -------------------------------------------------------------------------
	// Native storage of one object's transform: local position, local rotation
	// (Euler degrees) and local scale. Managed code reaches it through the
	// VspEngine.Transform reference handle, which owns nothing but the handle
	// returned by Scene::FindTransformHandle; every property read or write ends
	// up in this class.
	//
	// A transform always belongs to exactly one GameObject (its owner); the
	// owner handle is 0 while the transform is being created.
	// -------------------------------------------------------------------------
	class RUNTIME_API Transform : public NativeObject
	{
	public:
		// -------- Owner --------
		NativeObjectHandle GetOwnerGameObjectHandle() const { return m_uOwnerGameObjectHandle; }
		void SetOwnerGameObjectHandle(NativeObjectHandle uGameObjectHandle);

		// -------- Local position --------
		void GetLocalPosition(float& outPositionX, float& outPositionY, float& outPositionZ) const;
		void SetLocalPosition(float fPositionX, float fPositionY, float fPositionZ);

		// -------- Local rotation (Euler angles, degrees) --------
		void GetLocalRotation(float& outRotationX, float& outRotationY, float& outRotationZ) const;
		void SetLocalRotation(float fRotationX, float fRotationY, float fRotationZ);

		// -------- Local scale --------
		void GetLocalScale(float& outScaleX, float& outScaleY, float& outScaleZ) const;
		void SetLocalScale(float fScaleX, float fScaleY, float fScaleZ);

		// -------- Change tracking --------
		// Every setter raises the dirty flag; consumers (e.g. a render pipeline
		// building its per-frame draw list) clear it once they picked the value
		// up.
		bool IsDirty() const { return m_bIsDirty; }
		void ClearDirtyFlag() { m_bIsDirty = false; }

	private:
		NativeObjectHandle m_uOwnerGameObjectHandle = k_nInvalidObjectHandle;

		float m_fLocalPositionX = 0.0f;
		float m_fLocalPositionY = 0.0f;
		float m_fLocalPositionZ = 0.0f;

		float m_fLocalRotationX = 0.0f;
		float m_fLocalRotationY = 0.0f;
		float m_fLocalRotationZ = 0.0f;

		float m_fLocalScaleX = 1.0f;
		float m_fLocalScaleY = 1.0f;
		float m_fLocalScaleZ = 1.0f;

		bool m_bIsDirty = true;
	};
}
