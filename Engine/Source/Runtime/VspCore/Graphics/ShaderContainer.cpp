#include "RuntimePCH.h"

#include <cstdio>
#include <cstring>

#include "Core/String/VspStringFormat.h"
#include "Graphics/ShaderContainer.h"

namespace Vsp
{
	namespace
	{
		// True when a range of the file lies inside it, so a malformed container
		// cannot make the reader look outside its own bytes.
		bool IsRangeInside(size_t nFileByteSize, uint32 uByteOffset, uint32 uByteSize, uint32 uAlignment)
		{
			if ((uByteOffset % uAlignment) != 0)
			{
				return false;
			}
			if (uByteOffset > nFileByteSize)
			{
				return false;
			}
			return uByteSize <= (nFileByteSize - uByteOffset);
		}

	}

	bool ShaderContainer::Load(const VspString& sFilePath, VspString& outErrorText)
	{
		m_Bytes.clear();
		m_Entries.Clear();
		m_Metadata = JsonValue();

		FILE* pFile = nullptr;
		fopen_s(&pFile, sFilePath.GetData(), "rb");
		if (pFile == nullptr)
		{
			outErrorText = "cannot read the shader container '" + sFilePath + "'";
			return false;
		}

		uint8 sBuffer[8192];
		size_t nReadByteCount = 0;
		while ((nReadByteCount = fread(sBuffer, 1, sizeof(sBuffer), pFile)) > 0)
		{
			m_Bytes.insert(m_Bytes.end(), sBuffer, sBuffer + nReadByteCount);
		}
		fclose(pFile);

		if (m_Bytes.size() < sizeof(Vsfo::Header))
		{
			outErrorText = "the shader container '" + sFilePath + "' is too small to hold a header";
			return false;
		}

		Vsfo::Header header = {};
		memcpy(&header, m_Bytes.data(), sizeof(Vsfo::Header));

		if (header.uMagic != Vsfo::k_nMagic)
		{
			outErrorText = "the shader container '" + sFilePath + "' does not start with the VSFO magic number";
			return false;
		}
		if (header.uVersion != Vsfo::k_nVersion)
		{
			outErrorText = "the shader container '" + sFilePath + "' has version " +
				VspFormat::Format("{}", header.uVersion) + ", but this engine reads version " +
				VspFormat::Format("{}", Vsfo::k_nVersion);
			return false;
		}
		if (header.uEntryCount == 0)
		{
			outErrorText = "the shader container '" + sFilePath + "' holds no module";
			return false;
		}

		const size_t nFileByteSize = m_Bytes.size();
		const uint32 uIndexTableByteSize =
			static_cast<uint32>(sizeof(Vsfo::IndexEntry) * header.uEntryCount);

		if (header.uIndexTableByteSize != uIndexTableByteSize ||
			!IsRangeInside(nFileByteSize, header.uIndexTableByteOffset, header.uIndexTableByteSize, 4u) ||
			!IsRangeInside(nFileByteSize, header.uDataSegmentByteOffset, header.uDataSegmentByteSize, 4u) ||
			!IsRangeInside(nFileByteSize, header.uMetadataByteOffset, header.uMetadataByteSize, 1u))
		{
			outErrorText = "the shader container '" + sFilePath + "' describes sections outside the file";
			return false;
		}

		// ---- The metadata the shader carries about itself ----
		const VspString sMetadataText(
			reinterpret_cast<const char*>(m_Bytes.data()) + header.uMetadataByteOffset,
			header.uMetadataByteSize);
		if (!JsonReader::Parse(sMetadataText.GetData(), m_Metadata, outErrorText))
		{
			outErrorText = "the metadata of the shader container '" + sFilePath + "' is not readable: " + outErrorText;
			return false;
		}

		// ---- The index table ----
		for (uint32 uEntryIndex = 0; uEntryIndex < header.uEntryCount; ++uEntryIndex)
		{
			Vsfo::IndexEntry indexEntry = {};
			memcpy(&indexEntry,
				m_Bytes.data() + header.uIndexTableByteOffset + (sizeof(Vsfo::IndexEntry) * uEntryIndex),
				sizeof(Vsfo::IndexEntry));

			Entry entry;
			if (!ReadEntry(indexEntry, entry, outErrorText))
			{
				outErrorText = "the shader container '" + sFilePath + "': " + outErrorText;
				m_Entries.Clear();
				return false;
			}
			m_Entries.Add(entry);
		}

		return true;
	}

	const ShaderContainer::Entry* ShaderContainer::GetEntry(uint32 uEntryIndex) const
	{
		return (uEntryIndex < m_Entries.GetSize()) ? &m_Entries[uEntryIndex] : nullptr;
	}

	bool ShaderContainer::ReadEntry(const Vsfo::IndexEntry& indexEntry, Entry& outEntry, VspString& outErrorText)
	{
		if (indexEntry.uStageIndex >= Vsfo::k_nStageCount)
		{
			outErrorText = "an index entry names the unknown stage " +
				VspFormat::Format("{}", indexEntry.uStageIndex);
			return false;
		}
		if (indexEntry.uSpirvByteSize < (5u * sizeof(uint32)) ||
			(indexEntry.uSpirvByteSize % sizeof(uint32)) != 0)
		{
			outErrorText = "the " + VspString(indexEntry.StageName) +
				" module of variant " + VspFormat::Format("{}", indexEntry.uVariantIndex) +
				" is not a SPIR-V module";
			return false;
		}
		if (indexEntry.uInputCount > Vsfo::k_nMaxVariableCount ||
			indexEntry.uOutputCount > Vsfo::k_nMaxVariableCount ||
			indexEntry.uResourceCount > Vsfo::k_nMaxResourceCount ||
			indexEntry.uPushConstantMemberCount > Vsfo::k_nMaxPushConstantMemberCount)
		{
			outErrorText = "the reflection of the " + VspString(indexEntry.StageName) +
				" module of variant " + VspFormat::Format("{}", indexEntry.uVariantIndex) +
				" exceeds what a shader container may hold";
			return false;
		}

		// Both the module and its reflection record are addressed relative to
		// the data segment, and both have to lie inside it.
		Vsfo::Header header = {};
		memcpy(&header, m_Bytes.data(), sizeof(Vsfo::Header));

		const uint64 nModuleOffset = static_cast<uint64>(header.uDataSegmentByteOffset) + indexEntry.uSpirvByteOffset;
		const uint64 nReflectionOffset = static_cast<uint64>(header.uDataSegmentByteOffset) + indexEntry.uReflectionByteOffset;
		const uint64 nDataSegmentEnd =
			static_cast<uint64>(header.uDataSegmentByteOffset) + header.uDataSegmentByteSize;

		if (nModuleOffset + indexEntry.uSpirvByteSize > nDataSegmentEnd ||
			nReflectionOffset + indexEntry.uReflectionByteSize > nDataSegmentEnd ||
			nReflectionOffset + sizeof(Vsfo::ReflectionHeader) > nDataSegmentEnd)
		{
			outErrorText = "an index entry addresses bytes outside the data segment";
			return false;
		}

		// The reflection record repeats the summary the index table carries; a
		// container whose two copies disagree is malformed.
		Vsfo::ReflectionHeader reflectionHeader = {};
		memcpy(&reflectionHeader, m_Bytes.data() + nReflectionOffset, sizeof(Vsfo::ReflectionHeader));
		if (reflectionHeader.uInputCount != indexEntry.uInputCount ||
			reflectionHeader.uOutputCount != indexEntry.uOutputCount ||
			reflectionHeader.uResourceCount != indexEntry.uResourceCount ||
			reflectionHeader.uPushConstantMemberCount != indexEntry.uPushConstantMemberCount ||
			reflectionHeader.uPushConstantByteSize != indexEntry.uPushConstantByteSize)
		{
			outErrorText = "an index entry disagrees with the reflection record it points at";
			return false;
		}

		const uint64 nResourcesOffset = nReflectionOffset + sizeof(Vsfo::ReflectionHeader) +
			(static_cast<uint64>(reflectionHeader.uInputCount + reflectionHeader.uOutputCount) * sizeof(Vsfo::Variable));
		const uint64 nPushConstantsOffset = nResourcesOffset +
			(static_cast<uint64>(reflectionHeader.uResourceCount) * sizeof(Vsfo::Resource));
		const uint64 nRecordEnd = nPushConstantsOffset +
			(static_cast<uint64>(reflectionHeader.uPushConstantMemberCount) * sizeof(Vsfo::PushConstantMember));

		if (nRecordEnd > nReflectionOffset + indexEntry.uReflectionByteSize ||
			nRecordEnd > nDataSegmentEnd)
		{
			outErrorText = "the reflection record of an index entry is truncated";
			return false;
		}

		outEntry.uStageIndex = indexEntry.uStageIndex;
		outEntry.uVariantIndex = indexEntry.uVariantIndex;
		outEntry.uPassIndex = indexEntry.uPassIndex;

		memcpy(outEntry.StageName, indexEntry.StageName, sizeof(outEntry.StageName));
		memcpy(outEntry.EntryPointName, indexEntry.EntryPointName, sizeof(outEntry.EntryPointName));
		memcpy(outEntry.PassName, indexEntry.PassName, sizeof(outEntry.PassName));
		memcpy(outEntry.VariantKey, indexEntry.VariantKey, sizeof(outEntry.VariantKey));

		outEntry.pSpirvWords = reinterpret_cast<const uint32*>(m_Bytes.data() + nModuleOffset);
		outEntry.uSpirvWordCount = indexEntry.uSpirvByteSize / sizeof(uint32);
		outEntry.uSpirvByteSize = indexEntry.uSpirvByteSize;

		outEntry.uInputCount = reflectionHeader.uInputCount;
		outEntry.uOutputCount = reflectionHeader.uOutputCount;
		outEntry.uResourceCount = reflectionHeader.uResourceCount;
		outEntry.uPushConstantMemberCount = reflectionHeader.uPushConstantMemberCount;
		outEntry.uPushConstantByteSize = reflectionHeader.uPushConstantByteSize;

		outEntry.pResources = reinterpret_cast<const Vsfo::Resource*>(m_Bytes.data() + nResourcesOffset);
		outEntry.pPushConstantMembers =
			reinterpret_cast<const Vsfo::PushConstantMember*>(m_Bytes.data() + nPushConstantsOffset);

		return true;
	}
}
