#include "RuntimePCH.h"

#include <cstring>

#include "Classes/Shader.h"

namespace Vsp
{
	const Shader::Property* Shader::GetProperty(uint32 uPropertyIndex) const
	{
		return (uPropertyIndex < m_Properties.GetSize()) ? &m_Properties[uPropertyIndex] : nullptr;
	}

	const Shader::Property* Shader::FindProperty(const char* pPropertyName) const
	{
		if (pPropertyName == nullptr)
		{
			return nullptr;
		}

		for (size_t nPropertyIndex = 0; nPropertyIndex < m_Properties.GetSize(); ++nPropertyIndex)
		{
			if (std::strcmp(m_Properties[nPropertyIndex].Name, pPropertyName) == 0)
			{
				return &m_Properties[nPropertyIndex];
			}
		}
		return nullptr;
	}

	bool Shader::AddProperty(const Property& property)
	{
		if (FindProperty(property.Name) != nullptr || m_Properties.GetSize() >= k_nMaxPropertyCount)
		{
			return false;
		}

		m_Properties.Add(property);
		return true;
	}

	const Shader::KeywordGroup* Shader::GetKeywordGroup(uint32 uGroupIndex) const
	{
		return (uGroupIndex < m_KeywordGroups.GetSize()) ? &m_KeywordGroups[uGroupIndex] : nullptr;
	}

	int32 Shader::FindKeywordGroupIndex(const char* pGroupName) const
	{
		if (pGroupName == nullptr)
		{
			return -1;
		}

		for (size_t nGroupIndex = 0; nGroupIndex < m_KeywordGroups.GetSize(); ++nGroupIndex)
		{
			if (std::strcmp(m_KeywordGroups[nGroupIndex].Name, pGroupName) == 0)
			{
				return static_cast<int32>(nGroupIndex);
			}
		}
		return -1;
	}

	bool Shader::AddKeywordGroup(const KeywordGroup& keywordGroup)
	{
		if (m_KeywordGroups.GetSize() >= k_nMaxKeywordGroupCount)
		{
			return false;
		}
		if (FindKeywordGroupIndex(keywordGroup.Name) >= 0)
		{
			return false;
		}

		m_KeywordGroups.Add(keywordGroup);
		return true;
	}

	const Shader::VariantModule* Shader::GetVariant(uint32 uVariantIndex) const
	{
		return (uVariantIndex < m_Variants.GetSize()) ? &m_Variants[uVariantIndex] : nullptr;
	}

	const Shader::VariantModule* Shader::ResolveVariant(const uint32* pKeywordStateIndices) const
	{
		if (m_Variants.IsEmpty())
		{
			return nullptr;
		}

		if (pKeywordStateIndices == nullptr)
		{
			return GetDefaultVariant();
		}

		for (size_t nVariantIndex = 0; nVariantIndex < m_Variants.GetSize(); ++nVariantIndex)
		{
			const VariantModule& variant = m_Variants[nVariantIndex];

			bool bMatches = true;
			for (size_t nGroupIndex = 0; nGroupIndex < m_KeywordGroups.GetSize(); ++nGroupIndex)
			{
				if (variant.uKeywordStateIndices[nGroupIndex] != pKeywordStateIndices[nGroupIndex])
				{
					bMatches = false;
					break;
				}
			}

			if (bMatches)
			{
				return &variant;
			}
		}

		// Nothing matched (the build stripped that combination): the default
		// variant keeps the shader drawable.
		return GetDefaultVariant();
	}

	Shader::VariantModule* Shader::GetMutableVariant(uint32 uVariantIndex)
	{
		return (uVariantIndex < m_Variants.GetSize()) ? &m_Variants[uVariantIndex] : nullptr;
	}

	const Shader::VariantModule* Shader::GetDefaultVariant() const
	{
		return m_Variants.IsEmpty() ? nullptr : &m_Variants[0];
	}

	Shader::VariantModule* Shader::AddVariant()
	{
		if (m_Variants.GetSize() >= k_nMaxVariantCount)
		{
			return nullptr;
		}
		return &m_Variants.Add(VariantModule());
	}

	bool Shader::StageModule::AddResource(const ResourceBinding& resource)
	{
		if (uResourceCount >= k_nMaxStageResourceCount)
		{
			return false;
		}

		Resources[uResourceCount] = resource;
		++uResourceCount;
		return true;
	}

	const Shader::ResourceBinding* Shader::StageModule::FindResource(uint32 uDescriptorSet, uint32 uBinding) const
	{
		for (uint32 uResourceIndex = 0; uResourceIndex < uResourceCount; ++uResourceIndex)
		{
			const ResourceBinding& resource = Resources[uResourceIndex];
			if (resource.uDescriptorSet == uDescriptorSet && resource.uBinding == uBinding)
			{
				return &resource;
			}
		}
		return nullptr;
	}
}
