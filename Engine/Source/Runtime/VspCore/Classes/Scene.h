#pragma once

#include "Classes/Component.h"
#include "Classes/GameObject.h"
#include "Classes/Object.h"
#include "Classes/Transform.h"
#include "Core/Core.h"
#include "Core/String/VspString.h"
#include "Core/Templates/ArrayList.h"

namespace Vsp
{
	// -------------------------------------------------------------------------
	// Scene
	// -------------------------------------------------------------------------
	// The native object storage behind every managed engine class. Managed
	// GameObjects, Transforms and Components are reference handles: they hold a
	// NativeObjectHandle and forward every read and write to the tables owned
	// by this class.
	//
	// The scene owns three append-only tables, one per object kind. A slot
	// whose object was released is marked free and its generation is bumped, so
	// handles into released objects stop resolving instead of addressing
	// whichever object recycles the slot. Destroying a game object also
	// releases its transform and every component attached to it.
	//
	// Every function is a plain data operation on the tables; nothing throws
	// and nothing allocates outside the table arrays.
	// -------------------------------------------------------------------------
#pragma warning(push)
#pragma warning(disable : 4251)   // ArrayList members: header-only template.
	class RUNTIME_API Scene
	{
	public:
		static Scene& Get();

		// Releases every object and resets the scene (engine shutdown).
		void Clear();

		// -------- Game objects --------
		// Creates a game object with its own transform and returns the game
		// object handle (0 when no slot could be acquired).
		NativeObjectHandle CreateGameObject(const VspString& sName);

		// Releases the game object, its transform and every attached
		// component. Returns false when the handle does not address a live
		// game object.
		bool DestroyGameObject(NativeObjectHandle uGameObjectHandle);

		GameObject* FindGameObject(NativeObjectHandle uGameObjectHandle);
		const GameObject* FindGameObject(NativeObjectHandle uGameObjectHandle) const;

		// -------- Transforms --------
		Transform* FindTransform(NativeObjectHandle uTransformHandle);
		const Transform* FindTransform(NativeObjectHandle uTransformHandle) const;

		// Transform handle of a game object (0 when the handle is not a live
		// game object).
		NativeObjectHandle FindGameObjectTransformHandle(NativeObjectHandle uGameObjectHandle) const;

		// -------- Components --------
		// Creates a component of the given kind and attaches it to the game
		// object. Returns the component handle (0 on failure).
		NativeObjectHandle CreateComponent(NativeObjectHandle uGameObjectHandle, ComponentKind eComponentKind);

		// Detaches and releases one component. Returns false when the handle
		// does not address a live component.
		bool DestroyComponent(NativeObjectHandle uComponentHandle);

		Component* FindComponent(NativeObjectHandle uComponentHandle);
		const Component* FindComponent(NativeObjectHandle uComponentHandle) const;

		// -------- Generic object access --------
		// Serves the managed Object base class: one entry point resolves any
		// handle regardless of its kind.
		bool IsValidHandle(NativeObjectHandle uHandle) const;
		const VspString& GetObjectName(NativeObjectHandle uHandle) const;
		bool SetObjectName(NativeObjectHandle uHandle, const VspString& sName);

		// -------- Frame iteration --------
		// Renderable components of active game objects, in table order. The
		// managed render pipeline walks this list to build its draw list.
		uint32 GetRenderableComponentCount() const;
		NativeObjectHandle GetRenderableComponentHandle(uint32 uRenderableIndex) const;

		// Number of live objects per kind (diagnostics and tests).
		uint32 GetLiveGameObjectCount() const;
		uint32 GetLiveTransformCount() const;
		uint32 GetLiveComponentCount() const;

	private:
		Scene() = default;

		// Returns the slot of a free (recycled or brand new) entry, binding the
		// object to it. Never returns nullptr: the tables grow on demand.
		template <typename ObjectType>
		ObjectType* AcquireObjectSlot(NativeObjectKind eKind, ArrayList<ObjectType>& Table, const VspString& sName);

		// Releases the object stored in the given slot (no-op when it is free).
		template <typename ObjectType>
		void ReleaseObjectSlot(ArrayList<ObjectType>& Table, uint32 uSlotIndex);

		// Resolves a handle against one table, validating kind, slot range and
		// generation.
		template <typename ObjectType>
		ObjectType* ResolveHandle(ArrayList<ObjectType>& Table, NativeObjectHandle uHandle, NativeObjectKind eKind);

		template <typename ObjectType>
		const ObjectType* ResolveHandle(const ArrayList<ObjectType>& Table, NativeObjectHandle uHandle, NativeObjectKind eKind) const;

		bool IsActiveGameObject(NativeObjectHandle uGameObjectHandle) const;

		ArrayList<GameObject> m_GameObjects;
		ArrayList<Transform> m_Transforms;
		ArrayList<Component> m_Components;

		// Returned by GetObjectName for handles that do not resolve.
		VspString m_sEmptyName;
	};
#pragma warning(pop)
}
