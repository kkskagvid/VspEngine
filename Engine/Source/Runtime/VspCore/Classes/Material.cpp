#include "RuntimePCH.h"

#include <cstring>

#include "Classes/Material.h"

namespace Vsp
{
	bool Material::InitializeFromShader(const Shader& shader)
	{
		m_PropertyValues.Clear();
		m_EnabledKeywords.Clear();

		for (uint32 uPropertyIndex = 0; uPropertyIndex < shader.GetPropertyCount(); ++uPropertyIndex)
		{
			const Shader::Property* pProperty = shader.GetProperty(uPropertyIndex);
			if (pProperty == nullptr)
			{
				continue;
			}

			PropertyValue propertyValue;
			memcpy(propertyValue.Name, pProperty->Name, sizeof(propertyValue.Name));
			propertyValue.eType = pProperty->eType;
			memcpy(propertyValue.fValues, pProperty->fDefaultValues, sizeof(propertyValue.fValues));
			m_PropertyValues.Add(propertyValue);
		}
		return true;
	}

	const Material::PropertyValue* Material::GetPropertyValue(uint32 uValueIndex) const
	{
		return (uValueIndex < m_PropertyValues.GetSize()) ? &m_PropertyValues[uValueIndex] : nullptr;
	}

	Material::PropertyValue* Material::FindMutablePropertyValue(const char* pPropertyName)
	{
		if (pPropertyName == nullptr)
		{
			return nullptr;
		}

		for (size_t nValueIndex = 0; nValueIndex < m_PropertyValues.GetSize(); ++nValueIndex)
		{
			if (std::strcmp(m_PropertyValues[nValueIndex].Name, pPropertyName) == 0)
			{
				return &m_PropertyValues[nValueIndex];
			}
		}
		return nullptr;
	}

	const Material::PropertyValue* Material::FindPropertyValue(const char* pPropertyName) const
	{
		return const_cast<Material*>(this)->FindMutablePropertyValue(pPropertyName);
	}

	bool Material::EnsurePropertyValue(const Shader& shader, const char* pPropertyName, PropertyValue*& outPropertyValue)
	{
		outPropertyValue = FindMutablePropertyValue(pPropertyName);
		if (outPropertyValue != nullptr)
		{
			return true;
		}

		// The material may be handed a property the shader declares but that was
		// not copied in yet (a material created before its shader was known).
		const Shader::Property* pProperty = shader.FindProperty(pPropertyName);
		if (pProperty == nullptr || m_PropertyValues.GetSize() >= Shader::k_nMaxPropertyCount)
		{
			return false;
		}

		PropertyValue propertyValue;
		memcpy(propertyValue.Name, pProperty->Name, sizeof(propertyValue.Name));
		propertyValue.eType = pProperty->eType;
		memcpy(propertyValue.fValues, pProperty->fDefaultValues, sizeof(propertyValue.fValues));
		outPropertyValue = &m_PropertyValues.Add(propertyValue);
		return true;
	}

	bool Material::SetFloat(const char* pPropertyName, float fValue)
	{
		PropertyValue* pPropertyValue = FindMutablePropertyValue(pPropertyName);
		if (pPropertyValue == nullptr)
		{
			return false;
		}

		pPropertyValue->fValues[0] = fValue;
		return true;
	}

	bool Material::GetFloat(const char* pPropertyName, float& outValue) const
	{
		const PropertyValue* pPropertyValue = FindPropertyValue(pPropertyName);
		if (pPropertyValue == nullptr)
		{
			return false;
		}

		outValue = pPropertyValue->fValues[0];
		return true;
	}

	bool Material::SetVector(const char* pPropertyName, const float* pValues)
	{
		if (pValues == nullptr)
		{
			return false;
		}

		PropertyValue* pPropertyValue = FindMutablePropertyValue(pPropertyName);
		if (pPropertyValue == nullptr)
		{
			return false;
		}

		memcpy(pPropertyValue->fValues, pValues, sizeof(pPropertyValue->fValues));
		return true;
	}

	bool Material::GetVector(const char* pPropertyName, float* outValues) const
	{
		if (outValues == nullptr)
		{
			return false;
		}

		const PropertyValue* pPropertyValue = FindPropertyValue(pPropertyName);
		if (pPropertyValue == nullptr)
		{
			return false;
		}

		memcpy(outValues, pPropertyValue->fValues, sizeof(pPropertyValue->fValues));
		return true;
	}

	bool Material::SetKeywordEnabled(const char* pKeyword, bool bIsEnabled)
	{
		if (pKeyword == nullptr || *pKeyword == '\0')
		{
			return false;
		}

		int32 nExistingIndex = -1;
		for (size_t nKeywordIndex = 0; nKeywordIndex < m_EnabledKeywords.GetSize(); ++nKeywordIndex)
		{
			if (m_EnabledKeywords[nKeywordIndex].Equals(pKeyword))
			{
				nExistingIndex = static_cast<int32>(nKeywordIndex);
				break;
			}
		}

		if (bIsEnabled)
		{
			if (nExistingIndex < 0)
			{
				if (m_EnabledKeywords.GetSize() >= k_nMaxKeywordCount)
				{
					return false;
				}
				m_EnabledKeywords.Add(VspString(pKeyword));
			}
			return true;
		}

		if (nExistingIndex >= 0)
		{
			m_EnabledKeywords.RemoveAt(static_cast<size_t>(nExistingIndex));
		}
		return true;
	}

	bool Material::IsKeywordEnabled(const char* pKeyword) const
	{
		if (pKeyword == nullptr)
		{
			return false;
		}

		for (size_t nKeywordIndex = 0; nKeywordIndex < m_EnabledKeywords.GetSize(); ++nKeywordIndex)
		{
			if (m_EnabledKeywords[nKeywordIndex].Equals(pKeyword))
			{
				return true;
			}
		}
		return false;
	}

	void Material::BuildKeywordStateIndices(const Shader& shader, uint32* outKeywordStateIndices) const
	{
		if (outKeywordStateIndices == nullptr)
		{
			return;
		}

		for (uint32 uGroupIndex = 0; uGroupIndex < shader.GetKeywordGroupCount(); ++uGroupIndex)
		{
			const Shader::KeywordGroup* pGroup = shader.GetKeywordGroup(uGroupIndex);
			outKeywordStateIndices[uGroupIndex] = 0;   // The group's default state.

			if (pGroup == nullptr)
			{
				continue;
			}

			for (uint32 uStateIndex = 0; uStateIndex < pGroup->uKeywordStateCount; ++uStateIndex)
			{
				// A group's states exclude each other, so the enabled keyword
				// decides the state; "_" is the "no keyword" state.
				if (IsKeywordEnabled(pGroup->KeywordStates[uStateIndex]))
				{
					outKeywordStateIndices[uGroupIndex] = uStateIndex;
					break;
				}
			}
		}
	}

	const Shader::VariantModule* Material::ResolveVariant(const Shader& shader) const
	{
		uint32 uKeywordStateIndices[Shader::k_nMaxKeywordGroupCount] = {};
		BuildKeywordStateIndices(shader, uKeywordStateIndices);
		return shader.ResolveVariant(uKeywordStateIndices);
	}
}
