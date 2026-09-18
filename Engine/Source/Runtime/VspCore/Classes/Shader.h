#pragma once

#include <vector>

#include "Classes/Object.h"
#include "Core/Core.h"
#include "Core/String/VspString.h"
#include "Core/Templates/ArrayList.h"
#include "Graphics/ShaderReflection.h"

namespace Vsp
{
	// -------------------------------------------------------------------------
	// Shader
	// -------------------------------------------------------------------------
	// Native storage of a compiled shader: the modules HLSLCC produced for every
	// variant of every pass, plus the shader's metadata (its name, its render
	// queue, the properties a material can set and the keyword groups that select
	// a variant).
	//
	// Managed code reaches it through the VspEngine.Shader reference handle; the
	// ShaderLibrary loads it from the manifest and the .spv files next to the
	// executable.
	// -------------------------------------------------------------------------
#pragma warning(push)
#pragma warning(disable : 4251)   // std::vector / ArrayList members.
	class RUNTIME_API Shader : public NativeObject
	{
	public:
		static constexpr uint32 k_nMaxKeywordGroupCount = 8;
		static constexpr uint32 k_nMaxKeywordStateCount = 16;
		static constexpr uint32 k_nMaxVariantCount = 64;
		static constexpr uint32 k_nMaxPassCount = 8;
		static constexpr uint32 k_nMaxPropertyCount = 32;
		static constexpr uint32 k_nMaxKeywordNameLength = 64;
		static constexpr uint32 k_nMaxPropertyNameLength = 64;
		static constexpr uint32 k_nMaxPropertyDisplayNameLength = 128;
		static constexpr uint32 k_nMaxEntryPointNameLength = 64;
		static constexpr uint32 k_nMaxVariantKeyLength = 128;

		// What a keyword group does with its unused variants (see HLSLCC's
		// ShaderDefinition.h for the full rules).
		enum class KeywordKind : uint32
		{
			Variant = 0,
			VariantLocal = 1,
			MultiVariant = 2,
			MultiVariantLocal = 3,
		};

		enum class PropertyType : uint32
		{
			Float = 0,
			Vector = 1,
			Color = 2,
			Texture = 3,
		};

		struct KeywordGroup
		{
			char Name[k_nMaxKeywordNameLength] = {};
			KeywordKind eKind = KeywordKind::Variant;
			uint32 uKeywordStateCount = 0;
			char KeywordStates[k_nMaxKeywordStateCount][k_nMaxKeywordNameLength] = {};

			// True when the build strips the variants nobody uses.
			bool IsStrippable() const { return eKind == KeywordKind::Variant || eKind == KeywordKind::VariantLocal; }
		};

		struct Property
		{
			char Name[k_nMaxPropertyNameLength] = {};
			char DisplayName[k_nMaxPropertyDisplayNameLength] = {};
			PropertyType eType = PropertyType::Float;
			float fDefaultValues[4] = {};
		};

		// One compiled stage: the SPIR-V module, its entry point and the
		// reflection summary the pipeline checks itself against.
		struct StageModule
		{
			char EntryPointName[k_nMaxEntryPointNameLength] = {};
			std::vector<uint32> SpirvWords;
			uint32 uInputCount = 0;
			uint32 uOutputCount = 0;
			uint32 uResourceCount = 0;
			uint32 uPushConstantByteSize = 0;

			bool IsValid() const { return SpirvWords.size() >= 5; }
			uint32 GetSpirvByteCount() const { return static_cast<uint32>(SpirvWords.size() * sizeof(uint32)); }
		};

		struct PassModule
		{
			uint32 uPassIndex = 0;
			char Name[k_nMaxKeywordNameLength] = {};
			StageModule Stages[k_nShaderStageCount];

			bool IsValid() const { return Stages[0].IsValid() || Stages[1].IsValid(); }
		};

		struct VariantModule
		{
			uint32 uVariantIndex = 0;
			char Key[k_nMaxVariantKeyLength] = {};
			uint32 uKeywordStateIndices[k_nMaxKeywordGroupCount] = {};
			uint32 uPassCount = 0;
			PassModule Passes[k_nMaxPassCount];

			bool IsDefaultKey() const { return Key[0] == '\0'; }
		};

		// -------- Metadata --------
		const VspString& GetShaderName() const { return m_sShaderName; }
		void SetShaderName(const VspString& sShaderName) { m_sShaderName = sShaderName; }

		uint32 GetRenderQueue() const { return m_uRenderQueue; }
		void SetRenderQueue(uint32 uRenderQueue) { m_uRenderQueue = uRenderQueue; }

		// -------- Properties --------
		uint32 GetPropertyCount() const { return static_cast<uint32>(m_Properties.GetSize()); }
		const Property* GetProperty(uint32 uPropertyIndex) const;
		const Property* FindProperty(const char* pPropertyName) const;
		bool AddProperty(const Property& property);

		// -------- Keyword groups --------
		uint32 GetKeywordGroupCount() const { return static_cast<uint32>(m_KeywordGroups.GetSize()); }
		const KeywordGroup* GetKeywordGroup(uint32 uGroupIndex) const;
		int32 FindKeywordGroupIndex(const char* pGroupName) const;
		bool AddKeywordGroup(const KeywordGroup& keywordGroup);

		// -------- Variants and modules --------
		uint32 GetVariantCount() const { return static_cast<uint32>(m_Variants.GetSize()); }
		const VariantModule* GetVariant(uint32 uVariantIndex) const;

		// Variant whose keyword states match the given selection; the first
		// variant when nothing matches (never null for a valid shader).
		const VariantModule* ResolveVariant(const uint32* pKeywordStateIndices) const;

		// Variant a material with no keyword at all selects.
		const VariantModule* GetDefaultVariant() const;

		// Adds one variant; the shader keeps at most k_nMaxVariantCount of them.
		VariantModule* AddVariant();

		bool IsValid() const { return GetVariantCount() > 0; }

	private:
		VspString m_sShaderName;
		uint32 m_uRenderQueue = 2000;
		ArrayList<Property> m_Properties;
		ArrayList<KeywordGroup> m_KeywordGroups;
		ArrayList<VariantModule> m_Variants;
	};
#pragma warning(pop)
}
