#include "RuntimePCH.h"

#include <cstdio>

#include "Classes/Scene.h"
#include "Common/PlatformMisc.h"
#include "Core/Json/JsonReader.h"
#include "Core/Logging/Log.h"
#include "Graphics/ShaderLibrary.h"

namespace Vsp
{
	static constexpr const char* kLogTag = "ShaderLibrary";

	ShaderLibrary& ShaderLibrary::Get()
	{
		static ShaderLibrary s_Instance;
		return s_Instance;
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

	bool ShaderLibrary::LoadStageModule(
		const VspString& sModulePath,
		Shader::StageModule& outStage,
		VspString& outErrorText) const
	{
		FILE* pFile = nullptr;
		fopen_s(&pFile, sModulePath.GetData(), "rb");
		if (pFile == nullptr)
		{
			outErrorText = "cannot read the shader module '" + sModulePath + "'";
			return false;
		}

		std::vector<uint8> moduleBytes;
		uint8 sBuffer[4096];
		size_t nReadByteCount = 0;
		while ((nReadByteCount = fread(sBuffer, 1, sizeof(sBuffer), pFile)) > 0)
		{
			moduleBytes.insert(moduleBytes.end(), sBuffer, sBuffer + nReadByteCount);
		}
		fclose(pFile);

		if (moduleBytes.size() < 5u * sizeof(uint32) || (moduleBytes.size() % sizeof(uint32)) != 0)
		{
			outErrorText = "the shader module '" + sModulePath + "' is not a SPIR-V module";
			return false;
		}

		outStage.SpirvWords.resize(moduleBytes.size() / sizeof(uint32));
		memcpy(outStage.SpirvWords.data(), moduleBytes.data(), moduleBytes.size());

		// Every SPIR-V module starts with the magic number 0x07230203.
		if (outStage.SpirvWords[0] != 0x07230203u)
		{
			outErrorText = "the shader module '" + sModulePath + "' does not start with the SPIR-V magic number";
			return false;
		}
		return true;
	}

	NativeObjectHandle ShaderLibrary::LoadShader(const VspString& sShaderName, VspString& outErrorText)
	{
		outErrorText = nullptr;

		const VspString sManifestPath = m_sShaderDirectory + "\\" + sShaderName + ".shader.json";

		JsonValue manifest;
		if (!JsonReader::ParseFile(sManifestPath, manifest, outErrorText))
		{
			return k_nInvalidObjectHandle;
		}

		Scene& scene = Scene::Get();
		const NativeObjectHandle uShaderHandle = scene.CreateShader(manifest.GetMemberString("name", sShaderName.GetData()));
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

		pShader->SetRenderQueue(manifest.GetMemberUInt32("renderQueue", 2000));

		// ---- Properties ----
		const JsonValue& properties = manifest["properties"];
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
		const JsonValue& keywordGroups = manifest["keywordGroups"];
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

		// ---- Variants ----
		const JsonValue& variants = manifest["variants"];
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

			const JsonValue& passes = variantJson["passes"];
			for (uint32 uPassJsonIndex = 0; uPassJsonIndex < passes.GetElementCount(); ++uPassJsonIndex)
			{
				if (pVariant->uPassCount >= Shader::k_nMaxPassCount)
				{
					break;
				}

				const JsonValue& passJson = passes.GetElement(uPassJsonIndex);
				Shader::PassModule& pass = pVariant->Passes[pVariant->uPassCount];
				pass.uPassIndex = passJson.GetMemberUInt32("index", uPassJsonIndex);
				strncpy_s(pass.Name, sizeof(pass.Name), passJson.GetMemberString("name", "Pass").GetData(), _TRUNCATE);

				const JsonValue& stages = passJson["stages"];
				for (uint32 uStageJsonIndex = 0; uStageJsonIndex < stages.GetElementCount(); ++uStageJsonIndex)
				{
					const JsonValue& stageJson = stages.GetElement(uStageJsonIndex);

					const VspString sStageName = stageJson.GetMemberString("stage", "vertex");
					const uint32 uStageIndex = sStageName.Equals("fragment") ? 1u : 0u;
					Shader::StageModule& stage = pass.Stages[uStageIndex];

					strncpy_s(stage.EntryPointName, sizeof(stage.EntryPointName),
						stageJson.GetMemberString("entryPoint", "").GetData(), _TRUNCATE);
					stage.uInputCount = stageJson.GetMemberUInt32("inputCount", 0);
					stage.uOutputCount = stageJson.GetMemberUInt32("outputCount", 0);
					stage.uResourceCount = stageJson.GetMemberUInt32("resourceCount", 0);
					stage.uPushConstantByteSize = stageJson.GetMemberUInt32("pushConstantByteSize", 0);

					const VspString sModulePath =
						m_sShaderDirectory + "\\" + stageJson.GetMemberString("file", "").GetData();
					if (!LoadStageModule(sModulePath, stage, outErrorText))
					{
						return k_nInvalidObjectHandle;
					}
				}

				++pVariant->uPassCount;
			}
		}

		if (!pShader->IsValid())
		{
			outErrorText = "the shader '" + sShaderName + "' manifest lists no variant";
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
