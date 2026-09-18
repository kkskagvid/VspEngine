#include "RuntimePCH.h"

#include "Classes/Component.h"

namespace Vsp
{
	void Component::ResetSceneLinks()
	{
		m_uOwnerGameObjectHandle = k_nInvalidObjectHandle;
		m_eComponentKind = ComponentKind::None;
		m_sScriptTypeName.Clear();
		m_RenderState = ComponentRenderState();
		m_RenderState.uMaterialHandle = k_nInvalidObjectHandle;
	}
}
