#pragma once

#include <vector>

#include "Core/Core.h"
#include "Core/Json/JsonReader.h"
#include "Core/String/VspString.h"
#include "Core/Templates/ArrayList.h"
#include "Graphics/ShaderReflection.h"
#include "Common/VsfoFormat.h"

namespace Vsp
{
	// -------------------------------------------------------------------------
	// ShaderContainer
	// -------------------------------------------------------------------------
	// Reads the .vsfo file HLSLCC writes: every SPIR-V module of a shader
	// together with the reflection of each of them.
	//
	// The file is an index table followed by a data segment (see
	// Shared/VsfoFormat.h). This reader keeps the whole file in memory and hands
	// out one ShaderContainerEntry per module, so the caller never does byte
	// arithmetic of its own - and everything it hands out points INTO that
	// buffer, which therefore has to outlive the entries.
	//
	// Every function reports failure through return values; nothing throws, and
	// a malformed container is refused with the reason instead of being read
	// past its end.
	// -------------------------------------------------------------------------
#pragma warning(push)
#pragma warning(disable : 4251)   // std::vector member.
	class RUNTIME_API ShaderContainer
	{
	public:
		// One module of the container, as the index table describes it: which
		// stage it is, how big it is, where it sits and what it reflects.
		struct Entry
		{
			uint32 uStageIndex = 0;        // Vsfo::k_nStageVertex / k_nStageFragment
			uint32 uVariantIndex = 0;
			uint32 uPassIndex = 0;

			char StageName[Vsfo::k_nStageNameLength] = {};
			char EntryPointName[Vsfo::k_nEntryPointNameLength] = {};
			char PassName[Vsfo::k_nPassNameLength] = {};
			char VariantKey[Vsfo::k_nVariantKeyLength] = {};

			// The module. Valid as long as the container is.
			const uint32* pSpirvWords = nullptr;
			uint32 uSpirvWordCount = 0;
			uint32 uSpirvByteSize = 0;

			// The reflection summary, straight out of the index table.
			uint32 uInputCount = 0;
			uint32 uOutputCount = 0;
			uint32 uResourceCount = 0;
			uint32 uPushConstantMemberCount = 0;
			uint32 uPushConstantByteSize = 0;

			// The descriptors the stage reads: name, kind, set, binding, count.
			const Vsfo::Resource* pResources = nullptr;
			const Vsfo::PushConstantMember* pPushConstantMembers = nullptr;
		};

		// Reads a container from disk. Returns false with outErrorText when the
		// file is missing, is not a container, or does not fit its own header.
		bool Load(const VspString& sFilePath, VspString& outErrorText);

		bool IsLoaded() const { return !m_Bytes.empty(); }
		uint32 GetEntryCount() const { return static_cast<uint32>(m_Entries.GetSize()); }
		const Entry* GetEntry(uint32 uEntryIndex) const;

		// What the shader says about itself: name, render queue, properties and
		// keyword groups. Null until a container was loaded.
		const JsonValue& GetMetadata() const { return m_Metadata; }

	private:
		// Points one entry at its module and reflection record.
		bool ReadEntry(const Vsfo::IndexEntry& indexEntry, Entry& outEntry, VspString& outErrorText);

		std::vector<uint8> m_Bytes;
		ArrayList<Entry> m_Entries;
		JsonValue m_Metadata;
	};
#pragma warning(pop)
}
