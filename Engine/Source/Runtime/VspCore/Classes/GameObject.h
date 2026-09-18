#pragma once

#include "Classes/Object.h"
#include "Classes/Transform.h"
#include "Core/Core.h"
#include "Core/Templates/ArrayList.h"

namespace Vsp
{
	// -------------------------------------------------------------------------
	// GameObject
	// -------------------------------------------------------------------------
	// Native storage of one scene object: its name, its activation state, its
	// layer and the handles of the components attached to it. The object
	// always owns exactly one Transform, which the Scene stores on its behalf
	// (m_uTransformHandle below addresses it).
	//
	// Managed code reaches this data through the VspEngine.GameObject
	// reference handle, which owns nothing but the handle handed out by the
	// Scene.
	// -------------------------------------------------------------------------
#pragma warning(push)
#pragma warning(disable : 4251)   // ArrayList member: header-only template.
	class RUNTIME_API GameObject : public NativeObject
	{
	public:
		// Handles of the components attached to this object, in attach order.
		const ArrayList<NativeObjectHandle>& GetComponentHandles() const { return m_ComponentHandles; }
		void AttachComponentHandle(NativeObjectHandle uComponentHandle);
		bool DetachComponentHandle(NativeObjectHandle uComponentHandle);

		// The transform every game object owns.
		NativeObjectHandle GetTransformHandle() const { return m_uTransformHandle; }
		void SetTransformHandle(NativeObjectHandle uTransformHandle);

		// -------- Activation / layer --------
		// An inactive object is skipped by the systems that walk the scene
		// (the render pipeline asks for active objects only).
		bool IsActiveSelf() const { return m_bIsActiveSelf; }
		void SetActiveSelf(bool bIsActive) { m_bIsActiveSelf = bIsActive; }

		int32 GetLayer() const { return m_nLayer; }
		void SetLayer(int32 nLayer) { m_nLayer = nLayer; }

		// Removes every component reference and the transform reference; called
		// by the Scene before the object is released.
		void ResetSceneLinks();

	private:
		ArrayList<NativeObjectHandle> m_ComponentHandles;
		NativeObjectHandle m_uTransformHandle = k_nInvalidObjectHandle;
		bool m_bIsActiveSelf = true;
		int32 m_nLayer = 0;
	};
#pragma warning(pop)
}
