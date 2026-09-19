#include "ShaderOutputWriter.h"

#include <cstdio>
#include <cstring>

namespace Hlslcc
{
	namespace
	{
		// Escapes the characters JSON strings may not carry literally.
		std::string EscapeJsonText(const char* pText)
		{
			std::string sEscaped;
			for (const char* pCursor = pText; pCursor != nullptr && *pCursor != '\0'; ++pCursor)
			{
				switch (*pCursor)
				{
				case '"':  sEscaped += "\\\""; break;
				case '\\': sEscaped += "\\\\"; break;
				case '\n': sEscaped += "\\n"; break;
				case '\r': sEscaped += "\\r"; break;
				case '\t': sEscaped += "\\t"; break;
				default:   sEscaped.push_back(*pCursor); break;
				}
			}
			return sEscaped;
		}

		void AppendJsonTextMember(std::string& outJson, const char* pKey, const char* pValue, const char* pIndent)
		{
			outJson += pIndent;
			outJson += "\"";
			outJson += pKey;
			outJson += "\": \"";
			outJson += EscapeJsonText(pValue);
			outJson += "\"";
		}

		const char* ToVariableClassJsonName(ShaderVariableClass eClass)
		{
			switch (eClass)
			{
			case ShaderVariableClass::Scalar:  return "scalar";
			case ShaderVariableClass::Vector:  return "vector";
			case ShaderVariableClass::Matrix:  return "matrix";
			case ShaderVariableClass::Struct:  return "struct";
			case ShaderVariableClass::Image:   return "image";
			case ShaderVariableClass::Sampler: return "sampler";
			default:                           return "unknown";
			}
		}

		void AppendVariableJson(std::string& outJson, const ShaderVariable& variable, const char* pIndent)
		{
			char sNumberBuffer[768] = {};
			outJson += pIndent;
			outJson += "{\n";

			AppendJsonTextMember(outJson, "name", variable.Name, (std::string(pIndent) + "  ").c_str());
			outJson += ",\n";

			std::snprintf(sNumberBuffer, sizeof(sNumberBuffer),
				"%s  \"location\": %u,\n%s  \"builtIn\": %s,\n%s  \"class\": \"%s\",\n"
				"%s  \"components\": %u,\n%s  \"byteSize\": %u\n",
				pIndent, variable.uLocation,
				pIndent, variable.bIsBuiltIn ? "true" : "false",
				pIndent, ToVariableClassJsonName(variable.Type.eClass),
				pIndent, variable.Type.uComponentCount,
				pIndent, variable.Type.uByteSize);
			outJson += sNumberBuffer;

			outJson += pIndent;
			outJson += "}";
		}

		void AppendReflectionJson(std::string& outJson, const ShaderStageReflection& reflection)
		{
			char sNumberBuffer[768] = {};

			outJson += "  {\n";
			AppendJsonTextMember(outJson, "stage", ToStageName(reflection.eStage), "    ");
			outJson += ",\n";
			AppendJsonTextMember(outJson, "entryPoint", reflection.EntryPointName, "    ");
			outJson += ",\n";

			outJson += "    \"inputs\": [";
			if (reflection.uInputCount == 0)
			{
				outJson += "],\n";
			}
			else
			{
				outJson += "\n";
				for (uint32_t uInputIndex = 0; uInputIndex < reflection.uInputCount; ++uInputIndex)
				{
					AppendVariableJson(outJson, reflection.Inputs[uInputIndex], "      ");
					outJson += (uInputIndex + 1u < reflection.uInputCount) ? ",\n" : "\n";
				}
				outJson += "    ],\n";
			}

			outJson += "    \"outputs\": [";
			if (reflection.uOutputCount == 0)
			{
				outJson += "],\n";
			}
			else
			{
				outJson += "\n";
				for (uint32_t uOutputIndex = 0; uOutputIndex < reflection.uOutputCount; ++uOutputIndex)
				{
					AppendVariableJson(outJson, reflection.Outputs[uOutputIndex], "      ");
					outJson += (uOutputIndex + 1u < reflection.uOutputCount) ? ",\n" : "\n";
				}
				outJson += "    ],\n";
			}

			outJson += "    \"resources\": [";
			if (reflection.uResourceCount == 0)
			{
				outJson += "],\n";
			}
			else
			{
				outJson += "\n";
				for (uint32_t uResourceIndex = 0; uResourceIndex < reflection.uResourceCount; ++uResourceIndex)
				{
					const ShaderResourceBinding& binding = reflection.Resources[uResourceIndex];
					outJson += "      { ";
					outJson += "\"name\": \"" + EscapeJsonText(binding.Name) + "\", ";
					outJson += "\"kind\": \"" + std::string(ToResourceKindName(binding.eKind)) + "\", ";
					std::snprintf(sNumberBuffer, sizeof(sNumberBuffer),
						"\"set\": %u, \"binding\": %u, \"descriptorCount\": %u, \"elementByteSize\": %u",
						binding.uDescriptorSet, binding.uBinding, binding.uDescriptorCount, binding.uElementByteSize);
					outJson += sNumberBuffer;
					outJson += " }";
					outJson += (uResourceIndex + 1u < reflection.uResourceCount) ? ",\n" : "\n";
				}
				outJson += "    ],\n";
			}

			outJson += "    \"pushConstants\": [";
			if (reflection.uPushConstantMemberCount == 0)
			{
				outJson += "],\n";
			}
			else
			{
				outJson += "\n";
				for (uint32_t uMemberIndex = 0; uMemberIndex < reflection.uPushConstantMemberCount; ++uMemberIndex)
				{
					const ShaderPushConstantMember& member = reflection.PushConstantMembers[uMemberIndex];
					std::snprintf(sNumberBuffer, sizeof(sNumberBuffer),
						"      { \"name\": \"%s\", \"offset\": %u, \"byteSize\": %u }",
						EscapeJsonText(member.Name).c_str(), member.uByteOffset, member.uByteSize);
					outJson += sNumberBuffer;
					outJson += (uMemberIndex + 1u < reflection.uPushConstantMemberCount) ? ",\n" : "\n";
				}
				outJson += "    ],\n";
			}

			std::snprintf(sNumberBuffer, sizeof(sNumberBuffer),
				"    \"inputCount\": %u,\n    \"outputCount\": %u,\n    \"resourceCount\": %u,\n"
				"    \"pushConstantMemberCount\": %u,\n    \"pushConstantByteSize\": %u\n",
				reflection.uInputCount, reflection.uOutputCount, reflection.uResourceCount,
				reflection.uPushConstantMemberCount, reflection.uPushConstantByteSize);
			outJson += sNumberBuffer;
			outJson += "  }";
		}

		// Writes a byte buffer to a file.
		bool WriteWholeFile(const std::string& sFilePath, const void* pData, size_t nByteCount)
		{
			std::FILE* pFile = nullptr;
#if defined(_MSC_VER)
			if (fopen_s(&pFile, sFilePath.c_str(), "wb") != 0)
			{
				pFile = nullptr;
			}
#else
			pFile = std::fopen(sFilePath.c_str(), "wb");
#endif
			if (pFile == nullptr)
			{
				return false;
			}

			const size_t nWrittenByteCount = (nByteCount > 0) ? std::fwrite(pData, 1, nByteCount, pFile) : 0;
			std::fclose(pFile);
			return nWrittenByteCount == nByteCount;
		}
	}

	std::string ShaderOutputWriter::BuildVariantFileSuffix(const CompiledVariant& variant)
	{
		// The default variant keeps the plain file names, so the modules of a
		// shader without keywords - and of the "everything off" variant - are
		// called <name>.vert.spv / <name>.frag.spv.
		if (variant.Key.IsDefault())
		{
			return std::string();
		}
		return "." + variant.Key.KeyText;
	}

	std::string ShaderOutputWriter::BuildStageSpirvPath(
		const std::string& sOutputDirectory,
		const std::string& sBaseName,
		const CompiledPass& pass,
		const CompiledVariant& variant,
		uint32_t uPassCount,
		ShaderStage eStage)
	{
		std::string sFileName = sBaseName;

		// The pass only shows up in the file name when the shader has more than
		// one, which keeps the common case short.
		if (uPassCount > 1)
		{
			sFileName += ".";
			sFileName += pass.Name;
		}

		sFileName += BuildVariantFileSuffix(variant);
		sFileName += ".";
		sFileName += ToStageFileExtension(eStage);
		sFileName += ".spv";

		return sOutputDirectory + "\\" + sFileName;
	}

	std::string ShaderOutputWriter::BuildManifestPath(const std::string& sOutputDirectory, const std::string& sBaseName)
	{
		return sOutputDirectory + "\\" + sBaseName + ".shader.json";
	}

	std::string ShaderOutputWriter::BuildReflectionPath(const std::string& sOutputDirectory, const std::string& sBaseName)
	{
		return sOutputDirectory + "\\" + sBaseName + ".reflection.json";
	}

	std::string ShaderOutputWriter::ExtractBaseName(const std::string& sFilePath)
	{
		const size_t nSeparatorOffset = sFilePath.find_last_of("\\\\/");
		const std::string sFileName = (nSeparatorOffset == std::string::npos)
			? sFilePath
			: sFilePath.substr(nSeparatorOffset + 1);

		const size_t nExtensionOffset = sFileName.find_last_of('.');
		return (nExtensionOffset == std::string::npos || nExtensionOffset == 0)
			? sFileName
			: sFileName.substr(0, nExtensionOffset);
	}

	std::string ShaderOutputWriter::ExtractDirectory(const std::string& sFilePath)
	{
		const size_t nSeparatorOffset = sFilePath.find_last_of("\\\\/");
		return (nSeparatorOffset == std::string::npos) ? std::string(".") : sFilePath.substr(0, nSeparatorOffset);
	}

	HlslccResult ShaderOutputWriter::WriteStageSpirv(
		const std::string& sFilePath,
		const CompiledStage& stage,
		std::string& outErrorText)
	{
		outErrorText.clear();
		if (!stage.IsValid())
		{
			outErrorText = "the stage holds no SPIR-V module";
			return HlslccResult::FailInvalidArgument;
		}

		if (!WriteWholeFile(sFilePath, stage.SpirvWords.data(), stage.GetSpirvByteCount()))
		{
			outErrorText = "cannot write the SPIR-V file '" + sFilePath + "'";
			return HlslccResult::FailWriteOutput;
		}
		return HlslccResult::Success;
	}

	namespace
	{
		// Everything a shader says about itself: what it is called, where it came
		// from, what a material can set on it and which keyword groups select its
		// variants. The manifest file and the container's metadata section both
		// start with this, so a reader parses one shape either way.
		// bIncludeVariantKeys adds the light variant list (index, keyword names
		// and the keyword state each group is in) a container needs to resolve
		// the variant a material selects. The manifest file does not need it: it
		// lists its variants in full right after.
		void AppendShaderDescriptionJson(
			std::string& sJson,
			const CompiledShader& shader,
			const std::string& sBaseName,
			bool bIncludeVariantKeys)
		{
			char sNumberBuffer[768] = {};

			sJson += "  \"version\": 1,\n";
			sJson += "  \"name\": \"" + EscapeJsonText(shader.ShaderName.c_str()) + "\",\n";
			sJson += "  \"source\": \"" + EscapeJsonText(shader.SourceName.c_str()) + "\",\n";
			sJson += "  \"baseName\": \"" + EscapeJsonText(sBaseName.c_str()) + "\",\n";

			std::snprintf(sNumberBuffer, sizeof(sNumberBuffer), "  \"renderQueue\": %u,\n", shader.uRenderQueue);
			sJson += sNumberBuffer;

			// ---- Properties ----
			sJson += "  \"properties\": [\n";
			for (size_t nPropertyIndex = 0; nPropertyIndex < shader.Properties.size(); ++nPropertyIndex)
			{
				const ShaderProperty& property = shader.Properties[nPropertyIndex];
				std::snprintf(sNumberBuffer, sizeof(sNumberBuffer),
					"    { \"name\": \"%s\", \"displayName\": \"%s\", \"type\": \"%s\", "
					"\"defaults\": [%g, %g, %g, %g] }",
					EscapeJsonText(property.Name).c_str(),
					EscapeJsonText(property.DisplayName).c_str(),
					ToPropertyTypeName(property.eType),
					property.fDefaultValues[0], property.fDefaultValues[1],
					property.fDefaultValues[2], property.fDefaultValues[3]);
				sJson += sNumberBuffer;
				sJson += (nPropertyIndex + 1u < shader.Properties.size()) ? ",\n" : "\n";
			}
			sJson += "  ],\n";

			// ---- Keyword groups ----
			sJson += "  \"keywordGroups\": [\n";
			for (size_t nGroupIndex = 0; nGroupIndex < shader.KeywordGroups.size(); ++nGroupIndex)
			{
				const ShaderKeywordGroup& group = shader.KeywordGroups[nGroupIndex];
				sJson += "    { \"name\": \"" + EscapeJsonText(group.Name) + "\", ";
				sJson += "\"kind\": \"" + std::string(ToKeywordKindName(group.eKind)) + "\", ";
				sJson += std::string("\"strippable\": ") + (group.IsStrippable() ? "true" : "false") + ", ";
				sJson += std::string("\"local\": ") + (IsKeywordKindLocal(group.eKind) ? "true" : "false") + ", ";
				sJson += "\"states\": [";
				for (uint32_t uStateIndex = 0; uStateIndex < group.uKeywordStateCount; ++uStateIndex)
				{
					sJson += "\"" + EscapeJsonText(group.KeywordStates[uStateIndex]) + "\"";
					if (uStateIndex + 1u < group.uKeywordStateCount)
					{
						sJson += ", ";
					}
				}
				sJson += "] }";
				sJson += (nGroupIndex + 1u < shader.KeywordGroups.size()) ? ",\n" : "\n";
			}
			sJson += "  ],\n";

			// ---- Passes and variants ----
			const uint32_t uPassCount = shader.GetDefaultVariant() != nullptr
				? static_cast<uint32_t>(shader.GetDefaultVariant()->Passes.size())
				: 0u;

			std::snprintf(sNumberBuffer, sizeof(sNumberBuffer), "  \"passCount\": %u,\n", uPassCount);
			sJson += sNumberBuffer;
			std::snprintf(sNumberBuffer, sizeof(sNumberBuffer), "  \"variantCount\": %u,\n",
				static_cast<uint32_t>(shader.Variants.size()));
			sJson += sNumberBuffer;

			// ---- Variants, by identity only ----
			if (bIncludeVariantKeys)
			{
				sJson += "  \"variants\": [\n";
				for (size_t nVariantIndex = 0; nVariantIndex < shader.Variants.size(); ++nVariantIndex)
				{
					const CompiledVariant& variant = shader.Variants[nVariantIndex];
					std::snprintf(sNumberBuffer, sizeof(sNumberBuffer), "    { \"index\": %u, \"key\": \"%s\", \"keywordStateIndices\": [",
						variant.uVariantIndex,
						EscapeJsonText(variant.Key.KeyText.c_str()).c_str());
					sJson += sNumberBuffer;

					for (size_t nGroupIndex = 0; nGroupIndex < shader.KeywordGroups.size(); ++nGroupIndex)
					{
						std::snprintf(sNumberBuffer, sizeof(sNumberBuffer), "%u",
							variant.Key.uKeywordStateIndices[nGroupIndex]);
						sJson += sNumberBuffer;
						if (nGroupIndex + 1u < shader.KeywordGroups.size())
						{
							sJson += ", ";
						}
					}
					sJson += "] }";
					sJson += (nVariantIndex + 1u < shader.Variants.size()) ? ",\n" : "\n";
				}

				// This is the last member of the container's metadata, but the
				// manifest continues with its full variant list - so only the
				// manifest needs the separating comma.
				sJson += bIncludeVariantKeys ? "  ]\n" : "  ],\n";
			}
		}
	}

	std::string ShaderOutputWriter::BuildShaderMetadataJson(
		const CompiledShader& shader,
		const std::string& sBaseName)
	{
		std::string sJson = "{\n";
		AppendShaderDescriptionJson(sJson, shader, sBaseName, true);
		sJson += "}\n";
		return sJson;
	}

	std::string ShaderOutputWriter::BuildShaderManifestJson(
		const CompiledShader& shader,
		const std::string& sBaseName)
	{
		char sNumberBuffer[768] = {};
		std::string sJson = "{\n";
		AppendShaderDescriptionJson(sJson, shader, sBaseName, false);

		// The module file names below carry the pass name only when a Pass has
		// to be told apart from its siblings.
		const uint32_t uPassCount = shader.GetDefaultVariant() != nullptr
			? static_cast<uint32_t>(shader.GetDefaultVariant()->Passes.size())
			: 0u;

		// ---- Variants ----
		sJson += "  \"variants\": [\n";
		for (size_t nVariantIndex = 0; nVariantIndex < shader.Variants.size(); ++nVariantIndex)
		{
			const CompiledVariant& variant = shader.Variants[nVariantIndex];

			sJson += "    {\n";
			std::snprintf(sNumberBuffer, sizeof(sNumberBuffer), "      \"index\": %u,\n", variant.uVariantIndex);
			sJson += sNumberBuffer;
			sJson += "      \"key\": \"" + EscapeJsonText(variant.Key.KeyText.c_str()) + "\",\n";

			std::snprintf(sNumberBuffer, sizeof(sNumberBuffer), "      \"keywordStateIndices\": [");
			sJson += sNumberBuffer;
			for (size_t nGroupIndex = 0; nGroupIndex < shader.KeywordGroups.size(); ++nGroupIndex)
			{
				std::snprintf(sNumberBuffer, sizeof(sNumberBuffer), "%u",
					variant.Key.uKeywordStateIndices[nGroupIndex]);
				sJson += sNumberBuffer;
				if (nGroupIndex + 1u < shader.KeywordGroups.size())
				{
					sJson += ", ";
				}
			}
			sJson += "],\n";

			sJson += "      \"passes\": [\n";
			for (size_t nPassIndex = 0; nPassIndex < variant.Passes.size(); ++nPassIndex)
			{
				const CompiledPass& pass = variant.Passes[nPassIndex];
				sJson += "        {\n";
				std::snprintf(sNumberBuffer, sizeof(sNumberBuffer), "          \"index\": %u,\n", pass.uPassIndex);
				sJson += sNumberBuffer;
				sJson += "          \"name\": \"" + EscapeJsonText(pass.Name) + "\",\n";
				sJson += "          \"stages\": [\n";

				bool bHasWrittenStage = false;
				for (uint32_t uStageIndex = 0; uStageIndex < k_nShaderStageCount; ++uStageIndex)
				{
					const CompiledStage& stage = pass.Stages[uStageIndex];
					if (!stage.IsValid())
					{
						continue;
					}

					if (bHasWrittenStage)
					{
						sJson += ",\n";
					}

					// The manifest names the module file relative to itself, so a
					// run directory can be moved without rewriting it.
					std::string sModuleFileName = sBaseName;
					if (uPassCount > 1)
					{
						sModuleFileName += "." + std::string(pass.Name);
					}
					sModuleFileName += BuildVariantFileSuffix(variant);
					sModuleFileName += "." + std::string(ToStageFileExtension(stage.eStage)) + ".spv";

					sJson += "            {\n";
					sJson += "              \"stage\": \"" + std::string(ToStageName(stage.eStage)) + "\",\n";
					sJson += "              \"entryPoint\": \"" + EscapeJsonText(stage.EntryPointName.c_str()) + "\",\n";
					sJson += "              \"file\": \"" + EscapeJsonText(sModuleFileName.c_str()) + "\",\n";
					std::snprintf(sNumberBuffer, sizeof(sNumberBuffer),
						"              \"byteCount\": %u,\n              \"inputCount\": %u,\n"
						"              \"outputCount\": %u,\n              \"resourceCount\": %u,\n"
						"              \"pushConstantByteSize\": %u,\n",
						stage.GetSpirvByteCount(),
						stage.Reflection.uInputCount,
						stage.Reflection.uOutputCount,
						stage.Reflection.uResourceCount,
						stage.Reflection.uPushConstantByteSize);
					sJson += sNumberBuffer;

					// The descriptor bindings the ENGINE assigned to this stage. The
					// engine checks them against its own set before it builds a
					// pipeline, so a shader that does not fit it is rejected by name
					// instead of failing inside the driver.
					sJson += "              \"resources\": [";
					if (stage.Reflection.uResourceCount == 0)
					{
						sJson += "]\n";
					}
					else
					{
						sJson += "\n";
						for (uint32_t uResourceIndex = 0; uResourceIndex < stage.Reflection.uResourceCount; ++uResourceIndex)
						{
							const ShaderResourceBinding& binding = stage.Reflection.Resources[uResourceIndex];
							sJson += "                { \"name\": \"" + EscapeJsonText(binding.Name) + "\", ";
							sJson += "\"kind\": \"" + std::string(ToResourceKindName(binding.eKind)) + "\", ";
							std::snprintf(sNumberBuffer, sizeof(sNumberBuffer),
								"\"set\": %u, \"binding\": %u, \"descriptorCount\": %u }",
								binding.uDescriptorSet, binding.uBinding, binding.uDescriptorCount);
							sJson += sNumberBuffer;
							sJson += (uResourceIndex + 1u < stage.Reflection.uResourceCount) ? ",\n" : "\n";
						}
						sJson += "              ]\n";
					}

					sJson += "            }";
					bHasWrittenStage = true;
				}

				sJson += "\n          ]\n";
				sJson += "        }";
				sJson += (nPassIndex + 1u < variant.Passes.size()) ? ",\n" : "\n";
			}
			sJson += "      ]\n";

			sJson += "    }";
			sJson += (nVariantIndex + 1u < shader.Variants.size()) ? ",\n" : "\n";
		}
		sJson += "  ]\n";
		sJson += "}\n";
		return sJson;
	}

	HlslccResult ShaderOutputWriter::WriteShaderManifest(
		const std::string& sFilePath,
		const CompiledShader& shader,
		const std::string& sBaseName,
		std::string& outErrorText)
	{
		outErrorText.clear();
		const std::string sJson = BuildShaderManifestJson(shader, sBaseName);
		if (!WriteWholeFile(sFilePath, sJson.data(), sJson.size()))
		{
			outErrorText = "cannot write the shader manifest '" + sFilePath + "'";
			return HlslccResult::FailWriteOutput;
		}
		return HlslccResult::Success;
	}

	std::string ShaderOutputWriter::BuildReflectionJson(const CompiledShader& shader)
	{
		std::string sJson;
		sJson += "{\n";
		sJson += "  \"source\": \"" + EscapeJsonText(shader.SourceName.c_str()) + "\",\n";
		sJson += "  \"shader\": \"" + EscapeJsonText(shader.ShaderName.c_str()) + "\",\n";
		sJson += "  \"stageCount\": " + std::to_string(shader.GetCompiledStageCount()) + ",\n";
		sJson += "  \"stages\": [\n";

		bool bHasWrittenStage = false;
		const CompiledVariant* pDefaultVariant = shader.GetDefaultVariant();
		if (pDefaultVariant != nullptr && !pDefaultVariant->Passes.empty())
		{
			for (uint32_t uStageIndex = 0; uStageIndex < k_nShaderStageCount; ++uStageIndex)
			{
				const CompiledStage& stage = pDefaultVariant->Passes.front().Stages[uStageIndex];
				if (!stage.IsValid())
				{
					continue;
				}

				if (bHasWrittenStage)
				{
					sJson += ",\n";
				}
				AppendReflectionJson(sJson, stage.Reflection);
				bHasWrittenStage = true;
			}
		}

		sJson += "\n  ]\n}\n";
		return sJson;
	}

	HlslccResult ShaderOutputWriter::WriteReflectionJson(
		const std::string& sFilePath,
		const CompiledShader& shader,
		std::string& outErrorText)
	{
		outErrorText.clear();
		const std::string sJson = BuildReflectionJson(shader);
		if (!WriteWholeFile(sFilePath, sJson.data(), sJson.size()))
		{
			outErrorText = "cannot write the reflection file '" + sFilePath + "'";
			return HlslccResult::FailWriteOutput;
		}
		return HlslccResult::Success;
	}

	std::string ShaderOutputWriter::BuildConsoleSummary(const CompiledShader& shader)
	{
		char sBuffer[512] = {};
		std::string sSummary;

		sSummary += "shader  : " + shader.ShaderName + "\n";
		sSummary += "source  : " + shader.SourceName + "\n";
		std::snprintf(sBuffer, sizeof(sBuffer), "queue   : %u\n", shader.uRenderQueue);
		sSummary += sBuffer;

		if (!shader.Properties.empty())
		{
			sSummary += "properties:\n";
			for (const ShaderProperty& property : shader.Properties)
			{
				std::snprintf(sBuffer, sizeof(sBuffer), "  %-20s %-8s default (%.3g, %.3g, %.3g, %.3g)\n",
					property.Name, ToPropertyTypeName(property.eType),
					property.fDefaultValues[0], property.fDefaultValues[1],
					property.fDefaultValues[2], property.fDefaultValues[3]);
				sSummary += sBuffer;
			}
		}

		std::vector<ShaderVariantKey> variantKeys;
		for (const CompiledVariant& variant : shader.Variants)
		{
			variantKeys.push_back(variant.Key);
		}
		if (!shader.KeywordGroups.empty())
		{
			sSummary += "variants (";
			sSummary += std::to_string(shader.Variants.size());
			sSummary += " kept):\n";
			sSummary += BuildVariantStripReport(shader.KeywordGroups, variantKeys);
		}

		for (const CompiledVariant& variant : shader.Variants)
		{
			const std::string sVariantText = variant.Key.IsDefault() ? std::string("default") : variant.Key.KeyText;
			sSummary += "  variant '" + sVariantText + "'\n";

			for (const CompiledPass& pass : variant.Passes)
			{
				if (variant.Passes.size() > 1)
				{
					sSummary += "    pass " + std::string(pass.Name) + "\n";
				}

				for (uint32_t uStageIndex = 0; uStageIndex < k_nShaderStageCount; ++uStageIndex)
				{
					const CompiledStage& stage = pass.Stages[uStageIndex];
					if (!stage.IsValid())
					{
						continue;
					}

					std::snprintf(sBuffer, sizeof(sBuffer),
						"    %-8s entry '%s': %u SPIR-V bytes, %u inputs, %u outputs, %u resources, push constants %u bytes\n",
						ToStageName(stage.eStage),
						stage.EntryPointName.c_str(),
						stage.GetSpirvByteCount(),
						stage.Reflection.uInputCount,
						stage.Reflection.uOutputCount,
						stage.Reflection.uResourceCount,
						stage.Reflection.uPushConstantByteSize);
					sSummary += sBuffer;

					// Where the engine put each resource of this stage. A shader
					// writes none of these numbers itself.
					for (uint32_t uResourceIndex = 0;
						uResourceIndex < stage.Reflection.uResourceCount;
						++uResourceIndex)
					{
						const ShaderResourceBinding& binding = stage.Reflection.Resources[uResourceIndex];
						std::snprintf(sBuffer, sizeof(sBuffer),
							"             '%s' %s, set %u binding %u\n",
							binding.Name,
							ToResourceKindName(binding.eKind),
							binding.uDescriptorSet,
							binding.uBinding);
						sSummary += sBuffer;
					}
				}
			}
		}

		return sSummary;
	}
}
