#pragma once

#include "Classes/Object.h"
#include "Core/Core.h"
#include "Core/String/VspString.h"

namespace Vsp
{
	// What kind of behaviour a component carries. The scene stores every
	// component in one table and uses the kind to decide which systems pick it
	// up.
	enum class ComponentKind : uint32
	{
		None = 0,
		Script = 1,   // Backs one managed VspEngine.ScriptBehaviour instance.
	};

	// -------------------------------------------------------------------------
	// ComponentRenderState
	// -------------------------------------------------------------------------
	// The per-object state a render pipeline reads when it builds a frame.
	// It lives on the component because the component is what a pipeline
	// actually draws in this engine iteration (the demo triangle of a script).
	// -------------------------------------------------------------------------
	struct ComponentRenderState
	{
		bool bIsRenderable = false;              // Script components contribute one triangle to the frame.
		int32 nColorMode = 3;                    // 0 red, 1 blue, 2 green, 3 multicolor.
		NativeObjectHandle uMaterialHandle = k_nInvalidObjectHandle;   // Material the component draws with.
	};

	// -------------------------------------------------------------------------
	// Component
	// -------------------------------------------------------------------------
	// Native storage of one component attached to a game object. Managed code
	// reaches it through a component reference handle (VspEngine.Component and
	// its derived classes own nothing but that handle).
	// -------------------------------------------------------------------------
	class RUNTIME_API Component : public NativeObject
	{
	public:
		ComponentKind GetComponentKind() const { return m_eComponentKind; }
		void SetComponentKind(ComponentKind eComponentKind) { m_eComponentKind = eComponentKind; }

		// The game object this component is attached to.
		NativeObjectHandle GetOwnerGameObjectHandle() const { return m_uOwnerGameObjectHandle; }
		void SetOwnerGameObjectHandle(NativeObjectHandle uGameObjectHandle) { m_uOwnerGameObjectHandle = uGameObjectHandle; }

		// Type name of the managed script behind a Script component.
		const VspString& GetScriptTypeName() const { return m_sScriptTypeName; }
		void SetScriptTypeName(const VspString& sScriptTypeName) { m_sScriptTypeName = sScriptTypeName; }

		// -------- Enable state --------
		bool IsEnabled() const { return m_bIsEnabled; }
		void SetEnabled(bool bIsEnabled) { m_bIsEnabled = bIsEnabled; }

		// -------- Render state --------
		const ComponentRenderState& GetRenderState() const { return m_RenderState; }
		ComponentRenderState& GetMutableRenderState() { return m_RenderState; }

		// Clears every reference the scene owns on this component's behalf.
		void ResetSceneLinks();

	private:
		NativeObjectHandle m_uOwnerGameObjectHandle = k_nInvalidObjectHandle;
		ComponentKind m_eComponentKind = ComponentKind::None;
		VspString m_sScriptTypeName;
		ComponentRenderState m_RenderState;
		bool m_bIsEnabled = true;
	};
}
