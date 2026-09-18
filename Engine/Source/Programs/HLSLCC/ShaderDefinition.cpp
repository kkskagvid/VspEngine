#include "ShaderDefinition.h"

#include <cctype>
#include <cstring>

namespace Hlslcc
{
	namespace
	{
		// Copies text into a fixed-size name buffer, always terminating it.
		void CopyName(const std::string& sText, char* pOutName, size_t nCapacity)
		{
			if (pOutName == nullptr || nCapacity == 0)
			{
				return;
			}

			const size_t nCopyByteCount = (sText.size() < nCapacity - 1u) ? sText.size() : (nCapacity - 1u);
			if (nCopyByteCount > 0)
			{
				std::memcpy(pOutName, sText.data(), nCopyByteCount);
			}
			pOutName[nCopyByteCount] = '\0';
		}

		// Case-insensitive comparison, so the Shader block can spell the keyword
		// kinds the way they read best ("Variant", "MultiVariantLocal") while the
		// pragmas stay lower case.
		bool EqualsIgnoreCase(const std::string& sText, const char* pExpected)
		{
			size_t nIndex = 0;
			for (; nIndex < sText.size() && pExpected[nIndex] != '\0'; ++nIndex)
			{
				const char cLeft = static_cast<char>(std::tolower(static_cast<unsigned char>(sText[nIndex])));
				const char cRight = static_cast<char>(std::tolower(static_cast<unsigned char>(pExpected[nIndex])));
				if (cLeft != cRight)
				{
					return false;
				}
			}
			return nIndex == sText.size() && pExpected[nIndex] == '\0';
		}

		// True when the state at the given index of a group defines a keyword.
		bool DoesVariantDefineState(const ShaderVariantKey& variantKey, uint32_t uGroupIndex, uint32_t uStateIndex)
		{
			return variantKey.uKeywordStateIndices[uGroupIndex] == uStateIndex;
		}
	}

	const char* ToKeywordKindName(ShaderKeywordKind eKind)
	{
		switch (eKind)
		{
		case ShaderKeywordKind::Variant:           return "variant";
		case ShaderKeywordKind::VariantLocal:      return "variant_local";
		case ShaderKeywordKind::MultiVariant:      return "multi_variant";
		case ShaderKeywordKind::MultiVariantLocal: return "multi_variant_local";
		default:                                   return "variant";
		}
	}

	bool ParseKeywordKind(const std::string& sName, ShaderKeywordKind& outKind)
	{
		if (EqualsIgnoreCase(sName, "variant"))             { outKind = ShaderKeywordKind::Variant; return true; }
		if (EqualsIgnoreCase(sName, "variant_local"))       { outKind = ShaderKeywordKind::VariantLocal; return true; }
		if (EqualsIgnoreCase(sName, "multi_variant"))       { outKind = ShaderKeywordKind::MultiVariant; return true; }
		if (EqualsIgnoreCase(sName, "multi_variant_local")) { outKind = ShaderKeywordKind::MultiVariantLocal; return true; }
		return false;
	}

	bool IsKeywordKindStrippable(ShaderKeywordKind eKind)
	{
		return eKind == ShaderKeywordKind::Variant || eKind == ShaderKeywordKind::VariantLocal;
	}

	bool IsKeywordKindLocal(ShaderKeywordKind eKind)
	{
		return eKind == ShaderKeywordKind::VariantLocal || eKind == ShaderKeywordKind::MultiVariantLocal;
	}

	bool IsNoKeywordState(const char* pKeywordStateName)
	{
		return pKeywordStateName == nullptr || pKeywordStateName[0] == '\0' ||
			std::strcmp(pKeywordStateName, k_sNoKeywordStateName) == 0;
	}

	void BuildKeywordGroupName(
		const std::vector<std::string>& sKeywordNames,
		char (&outName)[k_nMaxShaderKeywordLength])
	{
		std::string sGroupName;
		for (size_t nKeywordIndex = 0; nKeywordIndex < sKeywordNames.size(); ++nKeywordIndex)
		{
			if (nKeywordIndex > 0)
			{
				sGroupName += "|";
			}
			sGroupName += sKeywordNames[nKeywordIndex];
		}
		CopyName(sGroupName, outName, k_nMaxShaderKeywordLength);
	}

	ShaderKeywordGroup BuildKeywordGroup(
		const std::vector<std::string>& sKeywordNames,
		ShaderKeywordKind eKind)
	{
		ShaderKeywordGroup group;
		group.eKind = eKind;
		BuildKeywordGroupName(sKeywordNames, group.Name);

		// A single keyword without the "_" marker is a boolean keyword: it creates
		// an "off" state and an "on" state. Every other declaration is used as it
		// stands, so "A B" means "exactly one of A and B" and "_ A" means
		// "A or nothing".
		if (sKeywordNames.size() == 1 && !IsNoKeywordState(sKeywordNames[0].c_str()))
		{
			CopyName(k_sNoKeywordStateName, group.KeywordStates[0], k_nMaxShaderKeywordLength);
			CopyName(sKeywordNames[0], group.KeywordStates[1], k_nMaxShaderKeywordLength);
			group.uKeywordStateCount = 2;
			return group;
		}

		for (size_t nKeywordIndex = 0;
			nKeywordIndex < sKeywordNames.size() && group.uKeywordStateCount < k_nMaxShaderKeywordStateCount;
			++nKeywordIndex)
		{
			CopyName(sKeywordNames[nKeywordIndex], group.KeywordStates[group.uKeywordStateCount], k_nMaxShaderKeywordLength);
			++group.uKeywordStateCount;
		}

		if (group.uKeywordStateCount == 0)
		{
			CopyName(k_sNoKeywordStateName, group.KeywordStates[0], k_nMaxShaderKeywordLength);
			group.uKeywordStateCount = 1;
		}
		return group;
	}

	const char* ToPropertyTypeName(ShaderPropertyType eType)
	{
		switch (eType)
		{
		case ShaderPropertyType::Float:   return "Float";
		case ShaderPropertyType::Vector:  return "Vector";
		case ShaderPropertyType::Color:   return "Color";
		case ShaderPropertyType::Texture: return "Texture";
		default:                          return "Float";
		}
	}

	bool ParsePropertyType(const std::string& sName, ShaderPropertyType& outType)
	{
		if (sName == "Float" || sName == "Range") { outType = ShaderPropertyType::Float; return true; }
		if (sName == "Vector")                    { outType = ShaderPropertyType::Vector; return true; }
		if (sName == "Color")                     { outType = ShaderPropertyType::Color; return true; }
		if (sName == "Texture" || sName == "2D")  { outType = ShaderPropertyType::Texture; return true; }
		return false;
	}

	bool BuildShaderVariantKeys(
		const std::vector<ShaderKeywordGroup>& keywordGroups,
		const std::vector<std::string>& sUsedKeywordStateNames,
		std::vector<ShaderVariantKey>& outVariantKeys,
		std::string& outErrorText)
	{
		outErrorText.clear();
		outVariantKeys.clear();

		// Which states of each group survive the strip rules.
		std::vector<std::vector<uint32_t>> keptStateIndicesPerGroup;
		keptStateIndicesPerGroup.reserve(keywordGroups.size());

		for (const ShaderKeywordGroup& group : keywordGroups)
		{
			std::vector<uint32_t> keptStateIndices;

			if (!group.IsStrippable())
			{
				// multi_variant / multi_variant_local: everything is compiled and
				// stays in the build, whether it is used or not.
				for (uint32_t uStateIndex = 0; uStateIndex < group.uKeywordStateCount; ++uStateIndex)
				{
					keptStateIndices.push_back(uStateIndex);
				}
			}
			else
			{
				// variant / variant_local: only the states the build reported as
				// used survive; nothing reported means only the default state.
				for (uint32_t uStateIndex = 0; uStateIndex < group.uKeywordStateCount; ++uStateIndex)
				{
					for (const std::string& sUsedStateName : sUsedKeywordStateNames)
					{
						if (sUsedStateName == group.KeywordStates[uStateIndex])
						{
							keptStateIndices.push_back(uStateIndex);
							break;
						}
					}
				}

				if (keptStateIndices.empty())
				{
					keptStateIndices.push_back(0);
				}
			}

			keptStateIndicesPerGroup.push_back(keptStateIndices);
		}

		// Cartesian product of the surviving states.
		ShaderVariantKey currentKey;
		std::vector<ShaderVariantKey> variantKeys;
		variantKeys.push_back(currentKey);

		for (size_t nGroupIndex = 0; nGroupIndex < keywordGroups.size(); ++nGroupIndex)
		{
			const ShaderKeywordGroup& group = keywordGroups[nGroupIndex];
			std::vector<ShaderVariantKey> expandedKeys;

			for (const ShaderVariantKey& variantKey : variantKeys)
			{
				for (uint32_t uStateIndex : keptStateIndicesPerGroup[nGroupIndex])
				{
					if (expandedKeys.size() >= k_nMaxShaderVariantCount)
					{
						outErrorText = "the shader declares more than " +
							std::to_string(k_nMaxShaderVariantCount) + " variants; reduce its keyword groups";
						return false;
					}

					ShaderVariantKey expandedKey = variantKey;
					expandedKey.uKeywordStateIndices[nGroupIndex] = uStateIndex;
					expandedKeys.push_back(expandedKey);
				}
			}

			variantKeys.swap(expandedKeys);
		}

		// Turn the state indices into the keyword names the compiler is given.
		for (ShaderVariantKey& variantKey : variantKeys)
		{
			variantKey.KeyText.clear();
			variantKey.Defines.clear();

			for (size_t nGroupIndex = 0; nGroupIndex < keywordGroups.size(); ++nGroupIndex)
			{
				const ShaderKeywordGroup& group = keywordGroups[nGroupIndex];
				const uint32_t uStateIndex = variantKey.uKeywordStateIndices[nGroupIndex];
				if (uStateIndex >= group.uKeywordStateCount)
				{
					continue;
				}

				const char* pKeywordStateName = group.KeywordStates[uStateIndex];
				if (IsNoKeywordState(pKeywordStateName))
				{
					continue;
				}

				if (!variantKey.KeyText.empty())
				{
					variantKey.KeyText += "+";
				}
				variantKey.KeyText += pKeywordStateName;
				variantKey.Defines.push_back(pKeywordStateName);
			}
		}

		outVariantKeys = std::move(variantKeys);
		return true;
	}

	std::string BuildVariantStripReport(
		const std::vector<ShaderKeywordGroup>& keywordGroups,
		const std::vector<ShaderVariantKey>& keptVariantKeys)
	{
		std::string sReport;

		for (size_t nGroupIndex = 0; nGroupIndex < keywordGroups.size(); ++nGroupIndex)
		{
			const ShaderKeywordGroup& group = keywordGroups[nGroupIndex];

			std::string sKeptStates;
			for (uint32_t uStateIndex = 0; uStateIndex < group.uKeywordStateCount; ++uStateIndex)
			{
				bool bIsKept = false;
				for (const ShaderVariantKey& variantKey : keptVariantKeys)
				{
					if (variantKey.uKeywordStateIndices[nGroupIndex] == uStateIndex)
					{
						bIsKept = true;
						break;
					}
				}

				sKeptStates += " ";
				sKeptStates += group.KeywordStates[uStateIndex];
				if (!bIsKept)
				{
					sKeptStates += "(stripped)";
				}
			}

			sReport += "  ";
			sReport += ToKeywordKindName(group.eKind);
			sReport += " ";
			sReport += group.Name;
			sReport += ":";
			sReport += sKeptStates;
			sReport += "\n";
		}

		return sReport;
	}
}
