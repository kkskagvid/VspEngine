#include "RuntimePCH.h"

#include "Classes/Scene.h"
#include "Core/Logging/Log.h"

namespace Vsp
{
	static constexpr const char* kLogTag = "Scene";

	Scene& Scene::Get()
	{
		static Scene s_Instance;
		return s_Instance;
	}

	// -------------------------------------------------------------------------
	// Slot management
	// -------------------------------------------------------------------------

	template <typename ObjectType>
	ObjectType* Scene::AcquireObjectSlot(
		NativeObjectKind eKind,
		ArrayList<ObjectType>& Table,
		const VspString& sName)
	{
		// Recycle the first free slot; the object keeps its generation counter,
		// so handles into the previous occupant stop resolving.
		for (size_t nSlotIndex = 0; nSlotIndex < Table.GetSize(); ++nSlotIndex)
		{
			if (!Table[nSlotIndex].IsValid())
			{
				ObjectType& Object = Table[nSlotIndex];
				Object.UnbindFromSlot();   // Ages the generation of the recycled slot.
				Object.BindToSlot(eKind, static_cast<uint32>(nSlotIndex));
				Object.SetName(sName);
				return &Object;
			}
		}

		ObjectType& Object = Table.Add(ObjectType());
		Object.BindToSlot(eKind, static_cast<uint32>(Table.GetSize() - 1u));
		Object.SetName(sName);
		return &Object;
	}

	template <typename ObjectType>
	void Scene::ReleaseObjectSlot(ArrayList<ObjectType>& Table, uint32 uSlotIndex)
	{
		if (uSlotIndex < Table.GetSize())
		{
			Table[uSlotIndex].UnbindFromSlot();
		}
	}

	template <typename ObjectType>
	ObjectType* Scene::ResolveHandle(
		ArrayList<ObjectType>& Table,
		NativeObjectHandle uHandle,
		NativeObjectKind eKind)
	{
		if (uHandle == k_nInvalidObjectHandle || GetObjectHandleKind(uHandle) != eKind)
		{
			return nullptr;
		}

		const uint32 uSlotIndex = GetObjectHandleSlotIndex(uHandle);
		if (uSlotIndex >= Table.GetSize())
		{
			return nullptr;
		}

		ObjectType& Object = Table[uSlotIndex];
		if (Object.GetHandle() != uHandle)
		{
			return nullptr;   // Stale handle: the slot moved on to another object.
		}
		return &Object;
	}

	template <typename ObjectType>
	const ObjectType* Scene::ResolveHandle(
		const ArrayList<ObjectType>& Table,
		NativeObjectHandle uHandle,
		NativeObjectKind eKind) const
	{
		return const_cast<Scene*>(this)->ResolveHandle(
			const_cast<ArrayList<ObjectType>&>(Table), uHandle, eKind);
	}

	// -------------------------------------------------------------------------
	// Lifecycle
	// -------------------------------------------------------------------------

	void Scene::Clear()
	{
		m_GameObjects.Clear();
		m_Transforms.Clear();
		m_Components.Clear();
		m_Shaders.Clear();
		m_Materials.Clear();
		m_Cameras.Clear();
		m_Rigidbodies.Clear();
		m_Colliders.Clear();
	}

	// -------------------------------------------------------------------------
	// Game objects
	// -------------------------------------------------------------------------

	NativeObjectHandle Scene::CreateGameObject(const VspString& sName)
	{
		GameObject* pGameObject = AcquireObjectSlot(NativeObjectKind::GameObject, m_GameObjects, sName);
		if (pGameObject == nullptr)
		{
			LOG_ERROR(kLogTag, "Failed to acquire a game object slot.");
			return k_nInvalidObjectHandle;
		}

		// Every game object owns exactly one transform.
		Transform* pTransform = AcquireObjectSlot(NativeObjectKind::Transform, m_Transforms, sName);
		if (pTransform == nullptr)
		{
			pGameObject->UnbindFromSlot();
			LOG_ERROR(kLogTag, "Failed to acquire a transform slot.");
			return k_nInvalidObjectHandle;
		}

		pTransform->SetOwnerGameObjectHandle(pGameObject->GetHandle());
		pGameObject->SetTransformHandle(pTransform->GetHandle());
		return pGameObject->GetHandle();
	}

	bool Scene::DestroyGameObject(NativeObjectHandle uGameObjectHandle)
	{
		GameObject* pGameObject = FindGameObject(uGameObjectHandle);
		if (pGameObject == nullptr)
		{
			return false;
		}

		// Components first: they reference the game object they belong to.
		ArrayList<NativeObjectHandle> componentHandles = pGameObject->GetComponentHandles();
		for (size_t nComponentIndex = 0; nComponentIndex < componentHandles.GetSize(); ++nComponentIndex)
		{
			DestroyComponent(componentHandles[nComponentIndex]);
		}

		// The physics objects the object owns go with it. Leaving them behind
		// would keep an invisible shape in the simulation and, worse, keep a
		// rigidbody driving a transform that no longer exists.
		DestroyOwnedPhysicsObjects(uGameObjectHandle);

		const NativeObjectHandle uTransformHandle = pGameObject->GetTransformHandle();
		pGameObject->ResetSceneLinks();

		Transform* pTransform = FindTransform(uTransformHandle);
		if (pTransform != nullptr)
		{
			// The transform leaves the scene graph before it goes: a parent
			// that kept the handle would otherwise walk into a dead slot.
			pTransform->DetachFromParent();
			pTransform->SetOwnerGameObjectHandle(k_nInvalidObjectHandle);
			ReleaseObjectSlot(m_Transforms, GetObjectHandleSlotIndex(uTransformHandle));
		}

		ReleaseObjectSlot(m_GameObjects, GetObjectHandleSlotIndex(uGameObjectHandle));
		return true;
	}

	GameObject* Scene::FindGameObject(NativeObjectHandle uGameObjectHandle)
	{
		return ResolveHandle(m_GameObjects, uGameObjectHandle, NativeObjectKind::GameObject);
	}

	const GameObject* Scene::FindGameObject(NativeObjectHandle uGameObjectHandle) const
	{
		return ResolveHandle(m_GameObjects, uGameObjectHandle, NativeObjectKind::GameObject);
	}

	// -------------------------------------------------------------------------
	// Transforms
	// -------------------------------------------------------------------------

	Transform* Scene::FindTransform(NativeObjectHandle uTransformHandle)
	{
		return ResolveHandle(m_Transforms, uTransformHandle, NativeObjectKind::Transform);
	}

	const Transform* Scene::FindTransform(NativeObjectHandle uTransformHandle) const
	{
		return ResolveHandle(m_Transforms, uTransformHandle, NativeObjectKind::Transform);
	}

	NativeObjectHandle Scene::FindGameObjectTransformHandle(NativeObjectHandle uGameObjectHandle) const
	{
		const GameObject* pGameObject = FindGameObject(uGameObjectHandle);
		return pGameObject != nullptr ? pGameObject->GetTransformHandle() : k_nInvalidObjectHandle;
	}

	// -------------------------------------------------------------------------
	// Components
	// -------------------------------------------------------------------------

	NativeObjectHandle Scene::CreateComponent(NativeObjectHandle uGameObjectHandle, ComponentKind eComponentKind)
	{
		GameObject* pGameObject = FindGameObject(uGameObjectHandle);
		if (pGameObject == nullptr)
		{
			LOG_ERROR(kLogTag, "Cannot attach a component: the game object handle is not live.");
			return k_nInvalidObjectHandle;
		}

		Component* pComponent = AcquireObjectSlot(
			NativeObjectKind::Component, m_Components, pGameObject->GetName());
		if (pComponent == nullptr)
		{
			LOG_ERROR(kLogTag, "Failed to acquire a component slot.");
			return k_nInvalidObjectHandle;
		}

		pComponent->SetComponentKind(eComponentKind);
		pComponent->SetOwnerGameObjectHandle(uGameObjectHandle);

		// Script components take part in the frame the render pipeline builds.
		ComponentRenderState& renderState = pComponent->GetMutableRenderState();
		renderState.bIsRenderable = (eComponentKind == ComponentKind::Script);

		pGameObject->AttachComponentHandle(pComponent->GetHandle());
		return pComponent->GetHandle();
	}

	bool Scene::DestroyComponent(NativeObjectHandle uComponentHandle)
	{
		Component* pComponent = FindComponent(uComponentHandle);
		if (pComponent == nullptr)
		{
			return false;
		}

		GameObject* pGameObject = FindGameObject(pComponent->GetOwnerGameObjectHandle());
		if (pGameObject != nullptr)
		{
			pGameObject->DetachComponentHandle(uComponentHandle);
		}

		pComponent->ResetSceneLinks();
		ReleaseObjectSlot(m_Components, GetObjectHandleSlotIndex(uComponentHandle));
		return true;
	}

	Component* Scene::FindComponent(NativeObjectHandle uComponentHandle)
	{
		return ResolveHandle(m_Components, uComponentHandle, NativeObjectKind::Component);
	}

	const Component* Scene::FindComponent(NativeObjectHandle uComponentHandle) const
	{
		return ResolveHandle(m_Components, uComponentHandle, NativeObjectKind::Component);
	}

	// -------------------------------------------------------------------------
	// Cameras
	// -------------------------------------------------------------------------

	NativeObjectHandle Scene::CreateCamera(NativeObjectHandle uGameObjectHandle)
	{
		GameObject* pGameObject = FindGameObject(uGameObjectHandle);
		if (pGameObject == nullptr)
		{
			LOG_ERROR(kLogTag, "Cannot create a camera for the unknown game object handle {}.",
				static_cast<uint32>(uGameObjectHandle));
			return k_nInvalidObjectHandle;
		}

		Camera* pCamera = AcquireObjectSlot(NativeObjectKind::Camera, m_Cameras, pGameObject->GetName());
		if (pCamera == nullptr)
		{
			LOG_ERROR(kLogTag, "Failed to acquire a camera slot.");
			return k_nInvalidObjectHandle;
		}

		pCamera->SetOwnerGameObjectHandle(uGameObjectHandle);
		return pCamera->GetHandle();
	}

	bool Scene::DestroyCamera(NativeObjectHandle uCameraHandle)
	{
		Camera* pCamera = FindCamera(uCameraHandle);
		if (pCamera == nullptr)
		{
			return false;
		}

		pCamera->SetOwnerGameObjectHandle(k_nInvalidObjectHandle);
		ReleaseObjectSlot(m_Cameras, GetObjectHandleSlotIndex(uCameraHandle));
		return true;
	}

	Camera* Scene::FindCamera(NativeObjectHandle uCameraHandle)
	{
		return ResolveHandle(m_Cameras, uCameraHandle, NativeObjectKind::Camera);
	}

	const Camera* Scene::FindCamera(NativeObjectHandle uCameraHandle) const
	{
		return ResolveHandle(m_Cameras, uCameraHandle, NativeObjectKind::Camera);
	}

	uint32 Scene::GetLiveCameraCount() const
	{
		uint32 uLiveCount = 0;
		for (size_t nSlotIndex = 0; nSlotIndex < m_Cameras.GetSize(); ++nSlotIndex)
		{
			uLiveCount += m_Cameras[nSlotIndex].IsValid() ? 1u : 0u;
		}
		return uLiveCount;
	}

	NativeObjectHandle Scene::GetLiveCameraHandle(uint32 uLiveCameraIndex) const
	{
		uint32 uLiveIndex = 0;
		for (size_t nSlotIndex = 0; nSlotIndex < m_Cameras.GetSize(); ++nSlotIndex)
		{
			if (!m_Cameras[nSlotIndex].IsValid())
			{
				continue;
			}

			if (uLiveIndex == uLiveCameraIndex)
			{
				return m_Cameras[nSlotIndex].GetHandle();
			}
			++uLiveIndex;
		}
		return k_nInvalidObjectHandle;
	}

	// -------------------------------------------------------------------------
	// Physics
	// -------------------------------------------------------------------------

	NativeObjectHandle Scene::CreateRigidbody(NativeObjectHandle uGameObjectHandle)
	{
		GameObject* pGameObject = FindGameObject(uGameObjectHandle);
		if (pGameObject == nullptr)
		{
			LOG_ERROR(kLogTag, "Cannot create a rigidbody for the unknown game object handle {}.",
				static_cast<uint32>(uGameObjectHandle));
			return k_nInvalidObjectHandle;
		}

		Rigidbody* pRigidbody = AcquireObjectSlot(NativeObjectKind::Rigidbody, m_Rigidbodies, pGameObject->GetName());
		if (pRigidbody == nullptr)
		{
			LOG_ERROR(kLogTag, "Failed to acquire a rigidbody slot.");
			return k_nInvalidObjectHandle;
		}

		pRigidbody->SetOwnerGameObjectHandle(uGameObjectHandle);
		return pRigidbody->GetHandle();
	}

	bool Scene::DestroyRigidbody(NativeObjectHandle uRigidbodyHandle)
	{
		Rigidbody* pRigidbody = FindRigidbody(uRigidbodyHandle);
		if (pRigidbody == nullptr)
		{
			return false;
		}

		pRigidbody->SetOwnerGameObjectHandle(k_nInvalidObjectHandle);
		ReleaseObjectSlot(m_Rigidbodies, GetObjectHandleSlotIndex(uRigidbodyHandle));
		return true;
	}

	Rigidbody* Scene::FindRigidbody(NativeObjectHandle uRigidbodyHandle)
	{
		return ResolveHandle(m_Rigidbodies, uRigidbodyHandle, NativeObjectKind::Rigidbody);
	}

	const Rigidbody* Scene::FindRigidbody(NativeObjectHandle uRigidbodyHandle) const
	{
		return ResolveHandle(m_Rigidbodies, uRigidbodyHandle, NativeObjectKind::Rigidbody);
	}

	NativeObjectHandle Scene::CreateCollider(NativeObjectHandle uGameObjectHandle)
	{
		GameObject* pGameObject = FindGameObject(uGameObjectHandle);
		if (pGameObject == nullptr)
		{
			LOG_ERROR(kLogTag, "Cannot create a collider for the unknown game object handle {}.",
				static_cast<uint32>(uGameObjectHandle));
			return k_nInvalidObjectHandle;
		}

		Collider* pCollider = AcquireObjectSlot(NativeObjectKind::Collider, m_Colliders, pGameObject->GetName());
		if (pCollider == nullptr)
		{
			LOG_ERROR(kLogTag, "Failed to acquire a collider slot.");
			return k_nInvalidObjectHandle;
		}

		pCollider->SetOwnerGameObjectHandle(uGameObjectHandle);
		return pCollider->GetHandle();
	}

	bool Scene::DestroyCollider(NativeObjectHandle uColliderHandle)
	{
		Collider* pCollider = FindCollider(uColliderHandle);
		if (pCollider == nullptr)
		{
			return false;
		}

		pCollider->SetOwnerGameObjectHandle(k_nInvalidObjectHandle);
		ReleaseObjectSlot(m_Colliders, GetObjectHandleSlotIndex(uColliderHandle));
		return true;
	}

	Collider* Scene::FindCollider(NativeObjectHandle uColliderHandle)
	{
		return ResolveHandle(m_Colliders, uColliderHandle, NativeObjectKind::Collider);
	}

	const Collider* Scene::FindCollider(NativeObjectHandle uColliderHandle) const
	{
		return ResolveHandle(m_Colliders, uColliderHandle, NativeObjectKind::Collider);
	}

	void Scene::DestroyOwnedPhysicsObjects(NativeObjectHandle uGameObjectHandle)
	{
		for (size_t nSlotIndex = 0; nSlotIndex < m_Colliders.GetSize(); ++nSlotIndex)
		{
			if (m_Colliders[nSlotIndex].IsValid() &&
				m_Colliders[nSlotIndex].GetOwnerGameObjectHandle() == uGameObjectHandle)
			{
				m_Colliders[nSlotIndex].SetOwnerGameObjectHandle(k_nInvalidObjectHandle);
				ReleaseObjectSlot(m_Colliders, static_cast<uint32>(nSlotIndex));
			}
		}

		for (size_t nSlotIndex = 0; nSlotIndex < m_Rigidbodies.GetSize(); ++nSlotIndex)
		{
			if (m_Rigidbodies[nSlotIndex].IsValid() &&
				m_Rigidbodies[nSlotIndex].GetOwnerGameObjectHandle() == uGameObjectHandle)
			{
				m_Rigidbodies[nSlotIndex].SetOwnerGameObjectHandle(k_nInvalidObjectHandle);
				ReleaseObjectSlot(m_Rigidbodies, static_cast<uint32>(nSlotIndex));
			}
		}
	}

	uint32 Scene::GetLiveRigidbodyCount() const
	{
		uint32 uLiveCount = 0;
		for (size_t nSlotIndex = 0; nSlotIndex < m_Rigidbodies.GetSize(); ++nSlotIndex)
		{
			uLiveCount += m_Rigidbodies[nSlotIndex].IsValid() ? 1u : 0u;
		}
		return uLiveCount;
	}

	NativeObjectHandle Scene::GetLiveRigidbodyHandle(uint32 uLiveRigidbodyIndex) const
	{
		uint32 uLiveIndex = 0;
		for (size_t nSlotIndex = 0; nSlotIndex < m_Rigidbodies.GetSize(); ++nSlotIndex)
		{
			if (!m_Rigidbodies[nSlotIndex].IsValid())
			{
				continue;
			}

			if (uLiveIndex == uLiveRigidbodyIndex)
			{
				return m_Rigidbodies[nSlotIndex].GetHandle();
			}
			++uLiveIndex;
		}
		return k_nInvalidObjectHandle;
	}

	uint32 Scene::GetLiveColliderCount() const
	{
		uint32 uLiveCount = 0;
		for (size_t nSlotIndex = 0; nSlotIndex < m_Colliders.GetSize(); ++nSlotIndex)
		{
			uLiveCount += m_Colliders[nSlotIndex].IsValid() ? 1u : 0u;
		}
		return uLiveCount;
	}

	NativeObjectHandle Scene::GetLiveColliderHandle(uint32 uLiveColliderIndex) const
	{
		uint32 uLiveIndex = 0;
		for (size_t nSlotIndex = 0; nSlotIndex < m_Colliders.GetSize(); ++nSlotIndex)
		{
			if (!m_Colliders[nSlotIndex].IsValid())
			{
				continue;
			}

			if (uLiveIndex == uLiveColliderIndex)
			{
				return m_Colliders[nSlotIndex].GetHandle();
			}
			++uLiveIndex;
		}
		return k_nInvalidObjectHandle;
	}

	// -------------------------------------------------------------------------
	// Shaders
	// -------------------------------------------------------------------------

	NativeObjectHandle Scene::CreateShader(const VspString& sShaderName)
	{
		Shader* pShader = AcquireObjectSlot(NativeObjectKind::Shader, m_Shaders, sShaderName);
		if (pShader == nullptr)
		{
			LOG_ERROR(kLogTag, "Failed to acquire a shader slot.");
			return k_nInvalidObjectHandle;
		}

		pShader->SetShaderName(sShaderName);
		return pShader->GetHandle();
	}

	Shader* Scene::FindShader(NativeObjectHandle uShaderHandle)
	{
		return ResolveHandle(m_Shaders, uShaderHandle, NativeObjectKind::Shader);
	}

	const Shader* Scene::FindShader(NativeObjectHandle uShaderHandle) const
	{
		return ResolveHandle(m_Shaders, uShaderHandle, NativeObjectKind::Shader);
	}

	// -------------------------------------------------------------------------
	// Materials
	// -------------------------------------------------------------------------

	NativeObjectHandle Scene::CreateMaterial(NativeObjectHandle uShaderHandle)
	{
		Shader* pShader = FindShader(uShaderHandle);
		if (pShader == nullptr)
		{
			LOG_ERROR(kLogTag, "Cannot create a material: the shader handle is not live.");
			return k_nInvalidObjectHandle;
		}

		Material* pMaterial = AcquireObjectSlot(NativeObjectKind::Material, m_Materials, pShader->GetShaderName());
		if (pMaterial == nullptr)
		{
			LOG_ERROR(kLogTag, "Failed to acquire a material slot.");
			return k_nInvalidObjectHandle;
		}

		pMaterial->SetShaderHandle(uShaderHandle);
		pMaterial->InitializeFromShader(*pShader);
		return pMaterial->GetHandle();
	}

	bool Scene::DestroyMaterial(NativeObjectHandle uMaterialHandle)
	{
		Material* pMaterial = FindMaterial(uMaterialHandle);
		if (pMaterial == nullptr)
		{
			return false;
		}

		pMaterial->SetShaderHandle(k_nInvalidObjectHandle);
		ReleaseObjectSlot(m_Materials, GetObjectHandleSlotIndex(uMaterialHandle));
		return true;
	}

	Material* Scene::FindMaterial(NativeObjectHandle uMaterialHandle)
	{
		return ResolveHandle(m_Materials, uMaterialHandle, NativeObjectKind::Material);
	}

	const Material* Scene::FindMaterial(NativeObjectHandle uMaterialHandle) const
	{
		return ResolveHandle(m_Materials, uMaterialHandle, NativeObjectKind::Material);
	}

	// -------------------------------------------------------------------------
	// Generic object access
	// -------------------------------------------------------------------------

	bool Scene::IsValidHandle(NativeObjectHandle uHandle) const
	{
		switch (GetObjectHandleKind(uHandle))
		{
		case NativeObjectKind::GameObject: return FindGameObject(uHandle) != nullptr;
		case NativeObjectKind::Transform:  return FindTransform(uHandle) != nullptr;
		case NativeObjectKind::Component:  return FindComponent(uHandle) != nullptr;
		case NativeObjectKind::Shader:     return FindShader(uHandle) != nullptr;
		case NativeObjectKind::Material:   return FindMaterial(uHandle) != nullptr;
		case NativeObjectKind::Rigidbody:  return FindRigidbody(uHandle) != nullptr;
		case NativeObjectKind::Collider:   return FindCollider(uHandle) != nullptr;
		default:                           return false;
		}
	}

	const VspString& Scene::GetObjectName(NativeObjectHandle uHandle) const
	{
		switch (GetObjectHandleKind(uHandle))
		{
		case NativeObjectKind::GameObject:
		{
			const GameObject* pGameObject = FindGameObject(uHandle);
			return pGameObject != nullptr ? pGameObject->GetName() : m_sEmptyName;
		}
		case NativeObjectKind::Transform:
		{
			const Transform* pTransform = FindTransform(uHandle);
			return pTransform != nullptr ? pTransform->GetName() : m_sEmptyName;
		}
		case NativeObjectKind::Component:
		{
			const Component* pComponent = FindComponent(uHandle);
			return pComponent != nullptr ? pComponent->GetName() : m_sEmptyName;
		}
		case NativeObjectKind::Shader:
		{
			const Shader* pShader = FindShader(uHandle);
			return pShader != nullptr ? pShader->GetName() : m_sEmptyName;
		}
		case NativeObjectKind::Material:
		{
			const Material* pMaterial = FindMaterial(uHandle);
			return pMaterial != nullptr ? pMaterial->GetName() : m_sEmptyName;
		}
		case NativeObjectKind::Rigidbody:
		{
			const Rigidbody* pRigidbody = FindRigidbody(uHandle);
			return pRigidbody != nullptr ? pRigidbody->GetName() : m_sEmptyName;
		}
		case NativeObjectKind::Collider:
		{
			const Collider* pCollider = FindCollider(uHandle);
			return pCollider != nullptr ? pCollider->GetName() : m_sEmptyName;
		}
		default:
			return m_sEmptyName;
		}
	}

	bool Scene::SetObjectName(NativeObjectHandle uHandle, const VspString& sName)
	{
		NativeObject* pObject = nullptr;
		switch (GetObjectHandleKind(uHandle))
		{
		case NativeObjectKind::GameObject: pObject = FindGameObject(uHandle); break;
		case NativeObjectKind::Transform:  pObject = FindTransform(uHandle); break;
		case NativeObjectKind::Component:  pObject = FindComponent(uHandle); break;
		case NativeObjectKind::Shader:     pObject = FindShader(uHandle); break;
		case NativeObjectKind::Material:   pObject = FindMaterial(uHandle); break;
		case NativeObjectKind::Rigidbody:  pObject = FindRigidbody(uHandle); break;
		case NativeObjectKind::Collider:   pObject = FindCollider(uHandle); break;
		default: break;
		}

		if (pObject == nullptr)
		{
			return false;
		}

		pObject->SetName(sName);
		return true;
	}

	// -------------------------------------------------------------------------
	// Frame iteration
	// -------------------------------------------------------------------------

	bool Scene::IsActiveGameObject(NativeObjectHandle uGameObjectHandle) const
	{
		const GameObject* pGameObject = FindGameObject(uGameObjectHandle);
		return pGameObject != nullptr && pGameObject->IsActiveSelf();
	}

	uint32 Scene::GetRenderableComponentCount() const
	{
		uint32 uRenderableCount = 0;
		for (size_t nSlotIndex = 0; nSlotIndex < m_Components.GetSize(); ++nSlotIndex)
		{
			const Component& component = m_Components[nSlotIndex];
			if (!component.IsValid() || !component.IsEnabled() || !component.GetRenderState().bIsRenderable)
			{
				continue;
			}
			if (IsActiveGameObject(component.GetOwnerGameObjectHandle()))
			{
				++uRenderableCount;
			}
		}
		return uRenderableCount;
	}

	NativeObjectHandle Scene::GetRenderableComponentHandle(uint32 uRenderableIndex) const
	{
		uint32 uCurrentIndex = 0;
		for (size_t nSlotIndex = 0; nSlotIndex < m_Components.GetSize(); ++nSlotIndex)
		{
			const Component& component = m_Components[nSlotIndex];
			if (!component.IsValid() || !component.IsEnabled() || !component.GetRenderState().bIsRenderable)
			{
				continue;
			}
			if (!IsActiveGameObject(component.GetOwnerGameObjectHandle()))
			{
				continue;
			}

			if (uCurrentIndex == uRenderableIndex)
			{
				return component.GetHandle();
			}
			++uCurrentIndex;
		}
		return k_nInvalidObjectHandle;
	}

	uint32 Scene::GetLiveGameObjectCount() const
	{
		uint32 uLiveCount = 0;
		for (size_t nSlotIndex = 0; nSlotIndex < m_GameObjects.GetSize(); ++nSlotIndex)
		{
			uLiveCount += m_GameObjects[nSlotIndex].IsValid() ? 1u : 0u;
		}
		return uLiveCount;
	}

	NativeObjectHandle Scene::GetLiveGameObjectHandle(uint32 uLiveGameObjectIndex) const
	{
		uint32 uLiveIndex = 0;
		for (size_t nSlotIndex = 0; nSlotIndex < m_GameObjects.GetSize(); ++nSlotIndex)
		{
			if (!m_GameObjects[nSlotIndex].IsValid())
			{
				continue;
			}

			if (uLiveIndex == uLiveGameObjectIndex)
			{
				return m_GameObjects[nSlotIndex].GetHandle();
			}
			++uLiveIndex;
		}
		return k_nInvalidObjectHandle;
	}

	uint32 Scene::GetLiveTransformCount() const
	{
		uint32 uLiveCount = 0;
		for (size_t nSlotIndex = 0; nSlotIndex < m_Transforms.GetSize(); ++nSlotIndex)
		{
			uLiveCount += m_Transforms[nSlotIndex].IsValid() ? 1u : 0u;
		}
		return uLiveCount;
	}

	uint32 Scene::GetLiveComponentCount() const
	{
		uint32 uLiveCount = 0;
		for (size_t nSlotIndex = 0; nSlotIndex < m_Components.GetSize(); ++nSlotIndex)
		{
			uLiveCount += m_Components[nSlotIndex].IsValid() ? 1u : 0u;
		}
		return uLiveCount;
	}

	uint32 Scene::GetLiveShaderCount() const
	{
		uint32 uLiveCount = 0;
		for (size_t nSlotIndex = 0; nSlotIndex < m_Shaders.GetSize(); ++nSlotIndex)
		{
			uLiveCount += m_Shaders[nSlotIndex].IsValid() ? 1u : 0u;
		}
		return uLiveCount;
	}

	uint32 Scene::GetLiveMaterialCount() const
	{
		uint32 uLiveCount = 0;
		for (size_t nSlotIndex = 0; nSlotIndex < m_Materials.GetSize(); ++nSlotIndex)
		{
			uLiveCount += m_Materials[nSlotIndex].IsValid() ? 1u : 0u;
		}
		return uLiveCount;
	}
}
