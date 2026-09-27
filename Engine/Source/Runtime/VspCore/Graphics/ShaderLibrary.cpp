#include "RuntimePCH.h"

#include <cstdio>

#include "Classes/Scene.h"
#include "Common/PlatformMisc.h"
#include "Core/EngineServices.h"
#include "Core/Json/JsonReader.h"
#include "Core/Logging/Log.h"
#include "Graphics/ShaderBindings.h"
#include "Graphics/ShaderContainer.h"
#include "Graphics/ShaderLibrary.h"

namespace Vsp
{
	static constexpr const char* kLogTag = "ShaderLibrary";

	// -------------------------------------------------------------------------
	// Binding check
	// -------------------------------------------------------------------------
	// The engine owns the descriptor set (see Graphics/ShaderBindings.h): each
	// resource kind has exactly one binding, and HLSLCC numbers a shader's
	// resources to match. A shader that asks for a binding the engine does not
	// provide cannot be drawn with, so it is rejected here, by name, instead of
	// failing later inside the driver.
	// -------------------------------------------------------------------------
	static bool ValidateStageBindings(
		const VspString& sShaderName,
		const char* pStageName,
		const Shader::StageModule& stage,
		VspString& outErrorText)
	{
		char sMessage[512] = {};

		for (uint32 uResourceIndex = 0; uResourceIndex < stage.uResourceCount; ++uResourceIndex)
		{
			const Shader::ResourceBinding& resource = stage.Resources[uResourceIndex];

			uint32 uExpectedBinding = 0;
			switch (resource.eKind)
			{
			case ShaderResourceKind::UniformBuffer: uExpectedBinding = ShaderBindings::k_nCameraUniformBuffer; break;
			case ShaderResourceKind::SampledImage:  uExpectedBinding = ShaderBindings::k_nBindlessTextures;   break;
			case ShaderResourceKind::Sampler:       uExpectedBinding = ShaderBindings::k_nBindlessSampler;    break;
			default:
				snprintf(sMessage, sizeof(sMessage),
					"the shader '%s' reads '%s' (%s) in its %s stage, which the engine cannot provide",
					sShaderName.GetData(), resource.Name, ToShaderResourceKindName(resource.eKind), pStageName);
				outErrorText = sMessage;
				return false;
			}

			if (resource.uDescriptorSet != ShaderBindings::k_nDescriptorSet || resource.uBinding != uExpectedBinding)
			{
				snprintf(sMessage, sizeof(sMessage),
					"the shader '%s' reads '%s' (%s) in its %s stage at set %u binding %u, "
					"but the engine provides it at set %u binding %u",
					sShaderName.GetData(), resource.Name, ToShaderResourceKindName(resource.eKind), pStageName,
					resource.uDescriptorSet, resource.uBinding,
					ShaderBindings::k_nDescriptorSet, uExpectedBinding);
				outErrorText = sMessage;
				return false;
			}
		}
		return true;
	}

	ShaderLibrary& ShaderLibrary::Get()
	{
		// The registry owns this service: it is created here on first use,
		// reports a lookup from any thread but the one that created it, and is
		// destroyed explicitly by EngineServices::ShutdownAll().
		return EngineServices::GetService<ShaderLibrary>("ShaderLibrary");
	}

	void ShaderLibrary::UseDefaultShaderDirectory()
	{
		m_sShaderDirectory = PlatformMisc::GetExecutableDirectoryPath() + "\\Shaders";
	}

	const ShaderLibrary::Entry* ShaderLibrary::FindEntry(const VspString& sShaderName) const
	{
		for (size_t nEntryIndex = 0; nEntryIndex < m_Entries.GetSize(); ++nEntryIndex)
		{
			if (m_Entries[nEntryIndex].sName.Equals(sShaderName))
			{
				return &m_Entries[nEntryIndex];
			}
		}
		return nullptr;
	}

	// -------------------------------------------------------------------------
	// Loading
	// -------------------------------------------------------------------------
	// A module says which variant and which pass it belongs to by index, and the
	// container may hand them over in any order, so a module looks both up - and
	// creates them when it is the first one to name them.
	// -------------------------------------------------------------------------
	static Shader::VariantModule* FindOrAddVariant(Shader& shader, uint32 uVariantIndex)
	{
		for (uint32 uVariantSlot = 0; uVariantSlot < shader.GetVariantCount(); ++uVariantSlot)
		{
			Shader::VariantModule* pVariant = shader.GetMutableVariant(uVariantSlot);
			if (pVariant != nullptr && pVariant->uVariantIndex == uVariantIndex)
			{
				return pVariant;
			}
		}

		Shader::VariantModule* pAddedVariant = shader.AddVariant();
		if (pAddedVariant != nullptr)
		{
			pAddedVariant->uVariantIndex = uVariantIndex;
		}
		return pAddedVariant;
	}

	static Shader::PassModule* FindOrAddPass(Shader::VariantModule& variant, uint32 uPassIndex)
	{
		for (uint32 uPassSlot = 0; uPassSlot < variant.uPassCount; ++uPassSlot)
		{
			if (variant.Passes[uPassSlot].uPassIndex == uPassIndex)
			{
				return &variant.Passes[uPassSlot];
			}
		}

		if (variant.uPassCount >= Shader::k_nMaxPassCount)
		{
			return nullptr;
		}

		Shader::PassModule& pass = variant.Passes[variant.uPassCount];
		pass.uPassIndex = uPassIndex;
		snprintf(pass.Name, sizeof(pass.Name), "Pass%u", uPassIndex);
		++variant.uPassCount;
		return &pass;
	}

	NativeObjectHandle ShaderLibrary::LoadShader(const VspString& sShaderName, VspString& outErrorText)
	{
		outErrorText = nullptr;

		// The shader is ONE file: the container HLSLCC wrote, holding every
		// module of every kept variant together with its reflection. The engine
		// reads nothing else, so a game ships a single asset per shader.
		const VspString sContainerPath = m_sShaderDirectory + "\\" + sShaderName + Vsfo::k_sFileExtension;

		ShaderContainer container;
		if (!container.Load(sContainerPath, outErrorText))
		{
			return k_nInvalidObjectHandle;
		}

		const JsonValue& metadata = container.GetMetadata();

		Scene& scene = Scene::Get();
		const NativeObjectHandle uShaderHandle = scene.CreateShader(metadata.GetMemberString("name", sShaderName.GetData()));
		if (uShaderHandle == k_nInvalidObjectHandle)
		{
			outErrorText = "the scene refused a shader slot for '" + sShaderName + "'";
			return k_nInvalidObjectHandle;
		}

		Shader* pShader = scene.FindShader(uShaderHandle);
		if (pShader == nullptr)
		{
			outErrorText = "the shader object vanished while loading '" + sShaderName + "'";
			return k_nInvalidObjectHandle;
		}

		pShader->SetRenderQueue(metadata.GetMemberUInt32("renderQueue", 2000));

		// ---- Properties ----
		const JsonValue& properties = metadata["properties"];
		for (uint32 uPropertyIndex = 0; uPropertyIndex < properties.GetElementCount(); ++uPropertyIndex)
		{
			const JsonValue& propertyJson = properties.GetElement(uPropertyIndex);

			Shader::Property property;
			strncpy_s(property.Name, sizeof(property.Name),
				propertyJson.GetMemberString("name", "").GetData(), _TRUNCATE);
			strncpy_s(property.DisplayName, sizeof(property.DisplayName),
				propertyJson.GetMemberString("displayName", "").GetData(), _TRUNCATE);

			const VspString sType = propertyJson.GetMemberString("type", "Float");
			if (sType.Equals("Vector"))       { property.eType = Shader::PropertyType::Vector; }
			else if (sType.Equals("Color"))   { property.eType = Shader::PropertyType::Color; }
			else if (sType.Equals("Texture")) { property.eType = Shader::PropertyType::Texture; }
			else                              { property.eType = Shader::PropertyType::Float; }

			const JsonValue& defaults = propertyJson["defaults"];
			for (uint32 uValueIndex = 0; uValueIndex < 4; ++uValueIndex)
			{
				property.fDefaultValues[uValueIndex] = defaults.GetElement(uValueIndex).GetFloat();
			}

			pShader->AddProperty(property);
		}

		// ---- Keyword groups ----
		const JsonValue& keywordGroups = metadata["keywordGroups"];
		for (uint32 uGroupIndex = 0; uGroupIndex < keywordGroups.GetElementCount(); ++uGroupIndex)
		{
			const JsonValue& groupJson = keywordGroups.GetElement(uGroupIndex);

			Shader::KeywordGroup group;
			strncpy_s(group.Name, sizeof(group.Name), groupJson.GetMemberString("name", "").GetData(), _TRUNCATE);

			const VspString sKind = groupJson.GetMemberString("kind", "variant");
			if (sKind.Equals("variant_local"))             { group.eKind = Shader::KeywordKind::VariantLocal; }
			else if (sKind.Equals("multi_variant"))        { group.eKind = Shader::KeywordKind::MultiVariant; }
			else if (sKind.Equals("multi_variant_local"))  { group.eKind = Shader::KeywordKind::MultiVariantLocal; }
			else                                           { group.eKind = Shader::KeywordKind::Variant; }

			const JsonValue& states = groupJson["states"];
			for (uint32 uStateIndex = 0;
				uStateIndex < states.GetElementCount() && group.uKeywordStateCount < Shader::k_nMaxKeywordStateCount;
				++uStateIndex)
			{
				strncpy_s(group.KeywordStates[group.uKeywordStateCount], Shader::k_nMaxKeywordNameLength,
					states.GetElement(uStateIndex).GetString().GetData(), _TRUNCATE);
				++group.uKeywordStateCount;
			}

			pShader->AddKeywordGroup(group);
		}

		// ---- Variants, from the identity the metadata carries ----
		// The modules below arrive variant by variant; this is what gives a
		// variant its keyword key and the state each group is in, which is how a
		// material picks one.
		const JsonValue& variants = metadata["variants"];
		for (uint32 uVariantJsonIndex = 0; uVariantJsonIndex < variants.GetElementCount(); ++uVariantJsonIndex)
		{
			const JsonValue& variantJson = variants.GetElement(uVariantJsonIndex);

			Shader::VariantModule* pVariant = pShader->AddVariant();
			if (pVariant == nullptr)
			{
				LOG_WARNING(kLogTag, "Shader '{}' has more variants than the engine keeps; the rest is ignored.",
					sShaderName.GetData());
				break;
			}

			pVariant->uVariantIndex = variantJson.GetMemberUInt32("index", uVariantJsonIndex);
			strncpy_s(pVariant->Key, sizeof(pVariant->Key),
				variantJson.GetMemberString("key", "").GetData(), _TRUNCATE);

			const JsonValue& stateIndices = variantJson["keywordStateIndices"];
			for (uint32 uGroupIndex = 0;
				uGroupIndex < stateIndices.GetElementCount() && uGroupIndex < Shader::k_nMaxKeywordGroupCount;
				++uGroupIndex)
			{
				pVariant->uKeywordStateIndices[uGroupIndex] = stateIndices.GetElement(uGroupIndex).GetUInt32();
			}
		}

		// ---- The modules, straight out of the container's index table ----
		for (uint32 uEntryIndex = 0; uEntryIndex < container.GetEntryCount(); ++uEntryIndex)
		{
			const ShaderContainer::Entry* pEntry = container.GetEntry(uEntryIndex);
			if (pEntry == nullptr || pEntry->uStageIndex >= k_nShaderStageCount)
			{
				continue;
			}

			Shader::VariantModule* pVariant = FindOrAddVariant(*pShader, pEntry->uVariantIndex);
			if (pVariant == nullptr)
			{
				outErrorText = "the shader '" + sShaderName + "' describes more variants than the engine keeps";
				return k_nInvalidObjectHandle;
			}

			const uint32 uPassIndex = pEntry->uPassIndex;
			Shader::PassModule* pPass = FindOrAddPass(*pVariant, uPassIndex);
			if (pPass == nullptr)
			{
				outErrorText = "the shader '" + sShaderName + "' describes more passes than the engine keeps";
				return k_nInvalidObjectHandle;
			}

			strncpy_s(pPass->Name, sizeof(pPass->Name), pEntry->PassName, _TRUNCATE);

			Shader::StageModule& stage = pPass->Stages[pEntry->uStageIndex];
			strncpy_s(stage.EntryPointName, sizeof(stage.EntryPointName), pEntry->EntryPointName, _TRUNCATE);

			stage.uInputCount = pEntry->uInputCount;
			stage.uOutputCount = pEntry->uOutputCount;
			stage.uPushConstantByteSize = pEntry->uPushConstantByteSize;
			stage.uPushConstantMemberCount = pEntry->uPushConstantMemberCount;

			stage.SpirvWords.assign(pEntry->pSpirvWords, pEntry->pSpirvWords + pEntry->uSpirvWordCount);
			if (stage.SpirvWords[0] != 0x07230203u)
			{
				outErrorText = "the '" + sShaderName + "' container holds a module that is not SPIR-V";
				return k_nInvalidObjectHandle;
			}

			// The bindings HLSLCC assigned to this stage. Every stage of every
			// variant carries them, so a shader that does not fit the engine's
			// set is rejected no matter which variant is drawn.
			for (uint32 uResourceIndex = 0; uResourceIndex < pEntry->uResourceCount; ++uResourceIndex)
			{
				const Vsfo::Resource& containerResource = pEntry->pResources[uResourceIndex];

				Shader::ResourceBinding resource;
				strncpy_s(resource.Name, sizeof(resource.Name), containerResource.Name, _TRUNCATE);
				resource.eKind = static_cast<ShaderResourceKind>(containerResource.uKind);
				resource.uDescriptorSet = containerResource.uDescriptorSet;
				resource.uBinding = containerResource.uBinding;
				resource.uDescriptorCount = containerResource.uDescriptorCount;

				if (!stage.AddResource(resource))
				{
					char sMessage[256] = {};
					snprintf(sMessage, sizeof(sMessage),
						"the shader '%s' declares more than %u resources in its %s stage",
						sShaderName.GetData(), Shader::k_nMaxStageResourceCount, pEntry->StageName);
					outErrorText = sMessage;
					return k_nInvalidObjectHandle;
				}
			}

			if (!ValidateStageBindings(sShaderName, pEntry->StageName, stage, outErrorText))
			{
				return k_nInvalidObjectHandle;
			}
		}

		if (!pShader->IsValid())
		{
			outErrorText = "the shader '" + sShaderName + "' container holds no module";
			return k_nInvalidObjectHandle;
		}

		Entry entry;
		entry.sName = sShaderName;
		entry.uShaderHandle = uShaderHandle;
		m_Entries.Add(entry);

		LOG_INFO(kLogTag, "Loaded shader '{}' ({} variant(s), {} property(ies), {} keyword group(s)).",
			pShader->GetShaderName().GetData(),
			pShader->GetVariantCount(),
			pShader->GetPropertyCount(),
			pShader->GetKeywordGroupCount());
		return uShaderHandle;
	}


	NativeObjectHandle ShaderLibrary::FindOrLoadShader(const VspString& sShaderName, VspString& outErrorText)
	{
		outErrorText = nullptr;

		const Entry* pEntry = FindEntry(sShaderName);
		if (pEntry != nullptr && Scene::Get().FindShader(pEntry->uShaderHandle) != nullptr)
		{
			return pEntry->uShaderHandle;
		}

		if (m_sShaderDirectory.IsEmpty())
		{
			UseDefaultShaderDirectory();
		}
		return LoadShader(sShaderName, outErrorText);
	}

	NativeObjectHandle ShaderLibrary::CreateMaterial(NativeObjectHandle uShaderHandle, VspString& outErrorText)
	{
		outErrorText = nullptr;

		const NativeObjectHandle uMaterialHandle = Scene::Get().CreateMaterial(uShaderHandle);
		if (uMaterialHandle == k_nInvalidObjectHandle)
		{
			outErrorText = "the scene refused to create a material";
		}
		return uMaterialHandle;
	}
}
