#include "RuntimePCH.h"

#include "Classes/Object.h"

namespace Vsp
{
	void NativeObject::BindToSlot(NativeObjectKind eKind, uint32 uSlotIndex)
	{
		m_eKind = eKind;
		m_uHandle = MakeObjectHandle(eKind, m_uSlotGeneration, uSlotIndex);
	}

	void NativeObject::UnbindFromSlot()
	{
		m_eKind = NativeObjectKind::None;
		m_uHandle = k_nInvalidObjectHandle;
		m_uSlotGeneration = (m_uSlotGeneration + 1u) & k_nObjectHandleGenerationMask;
	}

	void NativeObject::SetName(const VspString& sName)
	{
		m_sName = sName;
	}
}
