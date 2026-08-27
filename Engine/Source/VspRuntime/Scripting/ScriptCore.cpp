#include "RuntimePCH.h"

#include "Scripting/ScriptCore.h"

namespace Vsp
{
	ScriptCore& ScriptCore::Get()
	{
		static ScriptCore s_Instance;
		return s_Instance;
	}

	ScriptCore::TransformEntry* ScriptCore::FindTransformEntry(uint32_t uInstanceId)
	{
		for (size_t nEntryIndex = 0; nEntryIndex < m_TransformEntries.GetSize(); ++nEntryIndex)
		{
			if (m_TransformEntries[nEntryIndex].uInstanceId == uInstanceId)
			{
				return &m_TransformEntries[nEntryIndex];
			}
		}
		return nullptr;
	}

	const ScriptCore::TransformEntry* ScriptCore::FindTransformEntry(uint32_t uInstanceId) const
	{
		for (size_t nEntryIndex = 0; nEntryIndex < m_TransformEntries.GetSize(); ++nEntryIndex)
		{
			if (m_TransformEntries[nEntryIndex].uInstanceId == uInstanceId)
			{
				return &m_TransformEntries[nEntryIndex];
			}
		}
		return nullptr;
	}

	void ScriptCore::SetTransformPosition(uint32_t uInstanceId, float fPositionX, float fPositionY)
	{
		TransformEntry* pEntry = FindTransformEntry(uInstanceId);
		if (pEntry == nullptr)
		{
			TransformEntry newEntry;
			newEntry.uInstanceId = uInstanceId;
			newEntry.fPositionX = fPositionX;
			newEntry.fPositionY = fPositionY;
			m_TransformEntries.Add(newEntry);
			return;
		}

		pEntry->fPositionX = fPositionX;
		pEntry->fPositionY = fPositionY;
	}

	bool ScriptCore::GetTransformPosition(uint32_t uInstanceId, float& outPositionX, float& outPositionY) const
	{
		const TransformEntry* pEntry = FindTransformEntry(uInstanceId);
		if (pEntry == nullptr)
		{
			return false;
		}

		outPositionX = pEntry->fPositionX;
		outPositionY = pEntry->fPositionY;
		return true;
	}

	void ScriptCore::SetColorMode(uint32_t uInstanceId, int32_t nColorMode)
	{
		// A single triangle is rendered in this prototype; the color mode is
		// global. The instance id is accepted for API symmetry with Transform.
		m_nColorMode = nColorMode;
	}

	int32_t ScriptCore::GetColorMode(uint32_t uInstanceId) const
	{
		return m_nColorMode;
	}
}
