#pragma once

#include <cstdint>

// -------------------------------------------------------------------------
// VSFO - the Vsp Shader Object container
// -------------------------------------------------------------------------
// The file a shader source compiles into: one .vsfo per .vsf, holding every
// SPIR-V module the build kept together with the reflection of each of them.
//
// The layout is an INDEX TABLE followed by a DATA SEGMENT, so a reader can walk
// the shader without parsing anything else:
//
//   +----------------+  Vsfo::Header
//   |    Header      |  where the three sections are, how many entries exist
//   +----------------+
//   |  Index table   |  Vsfo::IndexEntry[EntryCount]: WHICH STAGE a module
//   |                |  belongs to, HOW BIG it is, WHERE it sits and what its
//   |                |  reflection summarises to
//   +----------------+
//   |  Data segment  |  the SPIR-V modules and, behind each of them, its full
//   |                |  reflection record (Vsfo::ReflectionHeader + arrays)
//   +----------------+
//   |   Metadata     |  UTF-8 JSON: the shader's name, render queue,
//   |                |  properties and keyword groups (see the manifest
//   |                |  HLSLCC writes next to the container)
//   +----------------+
//
// Every offset is relative to the start of the file, except the two inside an
// index entry, which are relative to the start of the data segment. All numbers
// are little-endian 32-bit values, and every section starts on a 4-byte
// boundary, which is also what a SPIR-V module needs to be read into.
//
// This header is the single definition of the format: HLSLCC (which writes it)
// and the engine (which reads it) both compile it, so writer and reader can
// never disagree about a field.
// -------------------------------------------------------------------------
namespace Vsfo
{
	// "VSFO" as a little-endian 32-bit value.
	inline constexpr uint32_t k_nMagic = 0x4F465356u;

	// Bumped whenever the layout below changes incompatibly. A reader refuses a
	// container it does not know.
	inline constexpr uint32_t k_nVersion = 1;

	// Stage a module belongs to. The values are the stage indices of the engine
	// and of SPIR-V's execution models for a graphics pipeline.
	inline constexpr uint32_t k_nStageVertex = 0;
	inline constexpr uint32_t k_nStageFragment = 1;
	inline constexpr uint32_t k_nStageCount = 2;

	// Field sizes of the container. A name that does not fit is refused by the
	// writer rather than silently cut off.
	inline constexpr uint32_t k_nStageNameLength = 16;
	inline constexpr uint32_t k_nEntryPointNameLength = 64;
	inline constexpr uint32_t k_nPassNameLength = 64;
	inline constexpr uint32_t k_nVariantKeyLength = 128;
	inline constexpr uint32_t k_nVariableNameLength = 64;

	// Limits the reflection record of one stage is written with.
	inline constexpr uint32_t k_nMaxVariableCount = 32;
	inline constexpr uint32_t k_nMaxResourceCount = 32;
	inline constexpr uint32_t k_nMaxPushConstantMemberCount = 32;

	// Start of the file: where every section is and how many modules exist.
	struct Header
	{
		uint32_t uMagic;                 // k_nMagic
		uint32_t uVersion;               // k_nVersion
		uint32_t uHeaderByteSize;        // sizeof(Header)
		uint32_t uEntryCount;            // IndexEntry count

		uint32_t uIndexTableByteOffset;  // offset of the index table
		uint32_t uIndexTableByteSize;
		uint32_t uDataSegmentByteOffset;
		uint32_t uDataSegmentByteSize;

		uint32_t uMetadataByteOffset;    // UTF-8 JSON, not NUL terminated
		uint32_t uMetadataByteSize;
		uint32_t uTotalByteSize;
		uint32_t uFlags;                 // k_nFlagDebugInfo, ...
	};

	inline constexpr uint32_t k_nFlagDebugInfo = 1u << 0;

	// One module of the container: which stage it is, how big it is, where it
	// sits and what it reflects. Everything a reader needs to describe a shader
	// is either in this entry or addressed by it.
	struct IndexEntry
	{
		// Which module of the shader this entry describes.
		uint32_t uStageIndex;            // k_nStageVertex / k_nStageFragment
		uint32_t uVariantIndex;          // index of the variant inside the shader
		uint32_t uPassIndex;             // index of the Pass inside the variant

		// Where the module and its reflection record sit, relative to the start
		// of the data segment.
		uint32_t uSpirvByteOffset;
		uint32_t uSpirvByteSize;
		uint32_t uReflectionByteOffset;
		uint32_t uReflectionByteSize;

		// What the reflection of this module summarises to, repeated from the
		// record so that the index table alone answers "what does this stage
		// take in and what does it bind?".
		uint32_t uInputCount;
		uint32_t uOutputCount;
		uint32_t uResourceCount;
		uint32_t uPushConstantMemberCount;
		uint32_t uPushConstantByteSize;
		uint32_t uReserved;

		// Identification, so a container is readable without its metadata.
		char StageName[k_nStageNameLength];            // "vertex" / "fragment"
		char EntryPointName[k_nEntryPointNameLength];  // the function the stage runs
		char PassName[k_nPassNameLength];              // name of the Pass it came from
		char VariantKey[k_nVariantKeyLength];          // "" for the default variant
	};

	// Start of one stage's reflection record. The arrays follow it in this
	// order, each holding exactly the count from the header:
	//   Variable[InputCount], Variable[OutputCount],
	//   Resource[ResourceCount], PushConstantMember[PushConstantMemberCount]
	struct ReflectionHeader
	{
		uint32_t uInputCount;
		uint32_t uOutputCount;
		uint32_t uResourceCount;
		uint32_t uPushConstantMemberCount;
		uint32_t uPushConstantByteSize;
		uint32_t uReserved;
	};

	// One interface variable of a stage (an input or an output).
	struct Variable
	{
		char Name[k_nVariableNameLength];
		uint32_t uLocation;
		uint32_t uClass;             // Hlslcc::ShaderVariableClass / Vsp::ShaderVariableClass
		uint32_t uComponentCount;    // scalars in a vector, columns in a matrix
		uint32_t uByteSize;
		uint32_t uIsBuiltIn;         // 1 for SV_Position and friends
	};

	// One descriptor the stage reads. The engine assigned these numbers (see
	// VspCore/Graphics/ShaderBindings.h), and it checks them while loading.
	struct Resource
	{
		char Name[k_nVariableNameLength];
		uint32_t uKind;              // Hlslcc::ShaderResourceKind / Vsp::ShaderResourceKind
		uint32_t uDescriptorSet;
		uint32_t uBinding;
		uint32_t uDescriptorCount;   // 0 for an unsized runtime array
		uint32_t uElementByteSize;
	};

	// One member of the push-constant block.
	struct PushConstantMember
	{
		char Name[k_nVariableNameLength];
		uint32_t uByteOffset;
		uint32_t uByteSize;
	};

	static_assert(sizeof(Header) == 48, "the container header is 48 bytes");
	static_assert(sizeof(IndexEntry) == 324, "an index entry is 324 bytes");
	static_assert(sizeof(ReflectionHeader) == 24, "a reflection header is 24 bytes");
	static_assert(sizeof(Variable) == 84, "a reflection variable is 84 bytes");
	static_assert(sizeof(Resource) == 84, "a reflection resource is 84 bytes");
	static_assert(sizeof(PushConstantMember) == 72, "a push-constant member is 72 bytes");

	// Aligns a byte offset up to the alignment a section needs (4 bytes: enough
	// for every field of the format and for a SPIR-V module).
	inline constexpr uint32_t k_nSectionAlignment = 4;

	inline uint32_t AlignUp(uint32_t uByteOffset)
	{
		return (uByteOffset + (k_nSectionAlignment - 1u)) & ~(k_nSectionAlignment - 1u);
	}

	// Name of a stage as it appears in StageName.
	inline const char* GetStageName(uint32_t uStageIndex)
	{
		return (uStageIndex == k_nStageFragment) ? "fragment" : "vertex";
	}

	// File name extension of a container.
	inline constexpr const char* k_sFileExtension = ".vsfo";
}
