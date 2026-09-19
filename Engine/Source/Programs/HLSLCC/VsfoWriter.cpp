#include "VsfoWriter.h"

#include <cstdio>
#include <cstring>

#include "ShaderOutputWriter.h"
#include "Shared/VsfoFormat.h"

namespace Hlslcc
{
	namespace
	{
		// Copies a name into a fixed field of the container. Fails when it does
		// not fit, so a reader never sees a silently cut-off name.
		bool CopyNameField(
			char* pDestination,
			size_t nDestinationByteSize,
			const char* pSource,
			const char* pFieldName,
			std::string& outErrorText)
		{
			const char* pText = (pSource != nullptr) ? pSource : "";
			if (std::strlen(pText) >= nDestinationByteSize)
			{
				outErrorText = std::string("the ") + pFieldName + " '" + pText +
					"' does not fit the shader container";
				return false;
			}

			std::memcpy(pDestination, pText, std::strlen(pText) + 1u);
			return true;
		}

		// Appends one value in the container's byte order (little-endian, which
		// is what both the compiler and the engine are built for).
		template <typename TValue>
		void AppendBytes(std::vector<uint8_t>& outBytes, const TValue& value)
		{
			const uint8_t* pValueBytes = reinterpret_cast<const uint8_t*>(&value);
			outBytes.insert(outBytes.end(), pValueBytes, pValueBytes + sizeof(TValue));
		}

		template <typename TValue>
		void AppendArray(std::vector<uint8_t>& outBytes, const TValue* pValues, size_t nValueCount)
		{
			if (pValues == nullptr || nValueCount == 0)
			{
				return;
			}
			const uint8_t* pValueBytes = reinterpret_cast<const uint8_t*>(pValues);
			outBytes.insert(outBytes.end(), pValueBytes, pValueBytes + (sizeof(TValue) * nValueCount));
		}

		// Grows the vector to a byte count, filling with zeroes.
		void ResizeTo(std::vector<uint8_t>& outBytes, size_t nByteCount)
		{
			if (outBytes.size() < nByteCount)
			{
				outBytes.resize(nByteCount, 0u);
			}
		}

		// Writes a whole file in one call. fopen_s is used instead of a stream so
		// a failure is a plain return value, never an exception.
		bool WriteWholeFile(const std::string& sFilePath, const void* pData, size_t nByteCount)
		{
			std::FILE* pFile = nullptr;
			if (fopen_s(&pFile, sFilePath.c_str(), "wb") != 0 || pFile == nullptr)
			{
				return false;
			}

			const size_t nWrittenByteCount = (nByteCount > 0) ? std::fwrite(pData, 1, nByteCount, pFile) : 0;
			std::fclose(pFile);
			return nWrittenByteCount == nByteCount;
		}

		// One module, while the container is being assembled: where it sits and
		// what the index table has to say about it.
		struct PendingModule
		{
			const CompiledPass* pPass = nullptr;
			const CompiledStage* pStage = nullptr;
			uint32_t uPassIndex = 0;
			Vsfo::IndexEntry Entry;
		};

		// Fills the reflection record of one module and returns its byte size.
		uint32_t BuildReflectionRecord(
			const ShaderStageReflection& reflection,
			std::vector<uint8_t>& outBytes)
		{
			Vsfo::ReflectionHeader header;
			header.uInputCount = reflection.uInputCount;
			header.uOutputCount = reflection.uOutputCount;
			header.uResourceCount = reflection.uResourceCount;
			header.uPushConstantMemberCount = reflection.uPushConstantMemberCount;
			header.uPushConstantByteSize = reflection.uPushConstantByteSize;
			header.uReserved = 0;

			const size_t nRecordStart = outBytes.size();
			AppendBytes(outBytes, header);

			for (uint32_t uInputIndex = 0; uInputIndex < reflection.uInputCount; ++uInputIndex)
			{
				const ShaderVariable& variable = reflection.Inputs[uInputIndex];
				Vsfo::Variable entry = {};
				std::snprintf(entry.Name, sizeof(entry.Name), "%s", variable.Name);
				entry.uLocation = variable.uLocation;
				entry.uClass = static_cast<uint32_t>(variable.Type.eClass);
				entry.uComponentCount = variable.Type.uComponentCount;
				entry.uByteSize = variable.Type.uByteSize;
				entry.uIsBuiltIn = variable.bIsBuiltIn ? 1u : 0u;
				AppendBytes(outBytes, entry);
			}

			for (uint32_t uOutputIndex = 0; uOutputIndex < reflection.uOutputCount; ++uOutputIndex)
			{
				const ShaderVariable& variable = reflection.Outputs[uOutputIndex];
				Vsfo::Variable entry = {};
				std::snprintf(entry.Name, sizeof(entry.Name), "%s", variable.Name);
				entry.uLocation = variable.uLocation;
				entry.uClass = static_cast<uint32_t>(variable.Type.eClass);
				entry.uComponentCount = variable.Type.uComponentCount;
				entry.uByteSize = variable.Type.uByteSize;
				entry.uIsBuiltIn = variable.bIsBuiltIn ? 1u : 0u;
				AppendBytes(outBytes, entry);
			}

			for (uint32_t uResourceIndex = 0; uResourceIndex < reflection.uResourceCount; ++uResourceIndex)
			{
				const ShaderResourceBinding& binding = reflection.Resources[uResourceIndex];
				Vsfo::Resource entry = {};
				std::snprintf(entry.Name, sizeof(entry.Name), "%s", binding.Name);
				entry.uKind = static_cast<uint32_t>(binding.eKind);
				entry.uDescriptorSet = binding.uDescriptorSet;
				entry.uBinding = binding.uBinding;
				entry.uDescriptorCount = binding.uDescriptorCount;
				entry.uElementByteSize = binding.uElementByteSize;
				AppendBytes(outBytes, entry);
			}

			for (uint32_t uMemberIndex = 0; uMemberIndex < reflection.uPushConstantMemberCount; ++uMemberIndex)
			{
				const ShaderPushConstantMember& member = reflection.PushConstantMembers[uMemberIndex];
				Vsfo::PushConstantMember entry = {};
				std::snprintf(entry.Name, sizeof(entry.Name), "%s", member.Name);
				entry.uByteOffset = member.uByteOffset;
				entry.uByteSize = member.uByteSize;
				AppendBytes(outBytes, entry);
			}

			return static_cast<uint32_t>(outBytes.size() - nRecordStart);
		}
	}

	std::string VsfoWriter::BuildContainerPath(const std::string& sOutputDirectory, const std::string& sBaseName)
	{
		return sOutputDirectory + "\\" + sBaseName + Vsfo::k_sFileExtension;
	}

	HlslccResult VsfoWriter::BuildContainer(
		const CompiledShader& shader,
		const std::string& sBaseName,
		std::vector<uint8_t>& outBytes,
		std::string& outErrorText)
	{
		outBytes.clear();

		// ---- 1. Collect the modules the index table describes ----
		std::vector<PendingModule> modules;
		for (const CompiledVariant& variant : shader.Variants)
		{
			for (const CompiledPass& pass : variant.Passes)
			{
				for (uint32_t uStageIndex = 0; uStageIndex < k_nShaderStageCount; ++uStageIndex)
				{
					const CompiledStage& stage = pass.Stages[uStageIndex];
					if (!stage.IsValid())
					{
						continue;
					}

					PendingModule module;
					module.pPass = &pass;
					module.pStage = &stage;
					module.uPassIndex = pass.uPassIndex;
					module.Entry = Vsfo::IndexEntry();

					module.Entry.uStageIndex = uStageIndex;
					module.Entry.uVariantIndex = variant.uVariantIndex;
					module.Entry.uPassIndex = pass.uPassIndex;
					module.Entry.uInputCount = stage.Reflection.uInputCount;
					module.Entry.uOutputCount = stage.Reflection.uOutputCount;
					module.Entry.uResourceCount = stage.Reflection.uResourceCount;
					module.Entry.uPushConstantMemberCount = stage.Reflection.uPushConstantMemberCount;
					module.Entry.uPushConstantByteSize = stage.Reflection.uPushConstantByteSize;

					if (!CopyNameField(module.Entry.StageName, sizeof(module.Entry.StageName),
							Vsfo::GetStageName(uStageIndex), "stage name", outErrorText) ||
						!CopyNameField(module.Entry.EntryPointName, sizeof(module.Entry.EntryPointName),
							stage.EntryPointName.c_str(), "entry point name", outErrorText) ||
						!CopyNameField(module.Entry.PassName, sizeof(module.Entry.PassName),
							pass.Name, "pass name", outErrorText) ||
						!CopyNameField(module.Entry.VariantKey, sizeof(module.Entry.VariantKey),
							variant.Key.KeyText.c_str(), "variant key", outErrorText))
					{
						return HlslccResult::FailWriteOutput;
					}

					modules.push_back(module);
				}
			}
		}

		if (modules.empty())
		{
			outErrorText = "the shader produced no module to put into a container";
			return HlslccResult::FailWriteOutput;
		}

		// ---- 2. Lay out the data segment ----
		// Every module is followed by its reflection record; both start on a
		// 4-byte boundary, so a reader can map either of them directly.
		std::vector<uint8_t> dataSegment;
		for (PendingModule& module : modules)
		{
			ResizeTo(dataSegment, Vsfo::AlignUp(static_cast<uint32_t>(dataSegment.size())));

			module.Entry.uSpirvByteOffset = static_cast<uint32_t>(dataSegment.size());
			module.Entry.uSpirvByteSize = module.pStage->GetSpirvByteCount();
			AppendArray(dataSegment, module.pStage->SpirvWords.data(), module.pStage->SpirvWords.size());

			ResizeTo(dataSegment, Vsfo::AlignUp(static_cast<uint32_t>(dataSegment.size())));

			module.Entry.uReflectionByteOffset = static_cast<uint32_t>(dataSegment.size());
			module.Entry.uReflectionByteSize =
				BuildReflectionRecord(module.pStage->Reflection, dataSegment);
		}

		// ---- 3. Metadata: what the shader is, without its modules ----
		const std::string sMetadata = ShaderOutputWriter::BuildShaderMetadataJson(shader, sBaseName);

		// ---- 4. Assemble the container ----
		Vsfo::Header header = {};
		header.uMagic = Vsfo::k_nMagic;
		header.uVersion = Vsfo::k_nVersion;
		header.uHeaderByteSize = sizeof(Vsfo::Header);
		header.uEntryCount = static_cast<uint32_t>(modules.size());

		header.uIndexTableByteOffset = sizeof(Vsfo::Header);
		header.uIndexTableByteSize = static_cast<uint32_t>(sizeof(Vsfo::IndexEntry) * modules.size());

		header.uDataSegmentByteOffset = Vsfo::AlignUp(header.uIndexTableByteOffset + header.uIndexTableByteSize);
		header.uDataSegmentByteSize = static_cast<uint32_t>(dataSegment.size());

		header.uMetadataByteOffset = Vsfo::AlignUp(header.uDataSegmentByteOffset + header.uDataSegmentByteSize);
		header.uMetadataByteSize = static_cast<uint32_t>(sMetadata.size());

		header.uTotalByteSize = header.uMetadataByteOffset + header.uMetadataByteSize;
		header.uFlags = 0;

		outBytes.reserve(header.uTotalByteSize);
		AppendBytes(outBytes, header);

		for (const PendingModule& module : modules)
		{
			AppendBytes(outBytes, module.Entry);
		}

		ResizeTo(outBytes, header.uDataSegmentByteOffset);
		outBytes.insert(outBytes.end(), dataSegment.begin(), dataSegment.end());

		ResizeTo(outBytes, header.uMetadataByteOffset);
		outBytes.insert(outBytes.end(), sMetadata.begin(), sMetadata.end());

		return HlslccResult::Success;
	}

	HlslccResult VsfoWriter::WriteContainer(
		const std::string& sFilePath,
		const CompiledShader& shader,
		const std::string& sBaseName,
		std::string& outErrorText)
	{
		std::vector<uint8_t> containerBytes;
		if (BuildContainer(shader, sBaseName, containerBytes, outErrorText) != HlslccResult::Success)
		{
			return HlslccResult::FailWriteOutput;
		}

		if (!WriteWholeFile(sFilePath, containerBytes.data(), containerBytes.size()))
		{
			outErrorText = "cannot write the shader container '" + sFilePath + "'";
			return HlslccResult::FailWriteOutput;
		}
		return HlslccResult::Success;
	}

	std::string VsfoWriter::BuildContainerSummary(const CompiledShader& shader, const std::string& sBaseName)
	{
		char sBuffer[512] = {};
		std::string sSummary;

		for (const CompiledVariant& variant : shader.Variants)
		{
			const std::string sVariantText = variant.Key.IsDefault() ? std::string("default") : variant.Key.KeyText;
			for (const CompiledPass& pass : variant.Passes)
			{
				for (uint32_t uStageIndex = 0; uStageIndex < k_nShaderStageCount; ++uStageIndex)
				{
					const CompiledStage& stage = pass.Stages[uStageIndex];
					if (!stage.IsValid())
					{
						continue;
					}

					std::snprintf(sBuffer, sizeof(sBuffer),
						"  %-8s variant '%s', pass '%s', entry '%s': %u bytes, %u resources\n",
						ToStageName(static_cast<ShaderStage>(uStageIndex)),
						sVariantText.c_str(),
						pass.Name,
						stage.EntryPointName.c_str(),
						stage.GetSpirvByteCount(),
						stage.Reflection.uResourceCount);
					sSummary += sBuffer;
				}
			}
		}

		return sSummary;
	}
}
