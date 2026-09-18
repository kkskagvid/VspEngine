#pragma once

#include <cstdint>

namespace Hlslcc
{
	// -------------------------------------------------------------------------
	// Shader reflection
	// -------------------------------------------------------------------------
	// Description of what a compiled shader stage expects from the pipeline: its
	// interface variables, the descriptor bindings it reads and the layout of
	// its push-constant block. Everything is fixed-size plain data so reflection
	// never allocates and can be handed across the engine boundary unchanged.
	// -------------------------------------------------------------------------

	// Pipeline stage a compiled entry point belongs to.
	enum class ShaderStage : int32_t
	{
		Vertex = 0,
		Fragment = 1,
	};

	static constexpr uint32_t k_nShaderStageCount = 2;

	// Shader model profile the stage compiles against.
	const char* ToProfileName(ShaderStage eStage);

	// Short lower-case name used in file names and logs ("vertex"/"fragment").
	const char* ToStageName(ShaderStage eStage);

	// File-name extension HLSLCC writes for the stage ("vert"/"frag").
	const char* ToStageFileExtension(ShaderStage eStage);

	// What a reflected value is made of.
	enum class ShaderVariableClass : int32_t
	{
		Scalar = 0,
		Vector = 1,
		Matrix = 2,
		Struct = 3,
		Image = 4,
		Sampler = 5,
		Unknown = 6,
	};

	// The descriptor type a resource binding maps onto in Vulkan.
	enum class ShaderResourceKind : int32_t
	{
		Unknown = 0,
		UniformBuffer = 1,
		StorageBuffer = 2,
		SampledImage = 3,
		StorageImage = 4,
		Sampler = 5,
		CombinedImageSampler = 6,
	};

	const char* ToResourceKindName(ShaderResourceKind eKind);

	// Type of one reflected value.
	struct ShaderTypeInfo
	{
		ShaderVariableClass eClass = ShaderVariableClass::Unknown;
		uint32_t uComponentCount = 0;   // Scalars in a vector, columns in a matrix.
		uint32_t uRowCount = 0;         // Rows of a matrix (0 for non-matrices).
		uint32_t uByteSize = 0;         // Size of one element in bytes.
	};

	// One interface variable (shader input or output).
	static constexpr uint32_t k_nMaxShaderVariableNameLength = 128;
	static constexpr uint32_t k_nMaxShaderInputCount = 32;
	static constexpr uint32_t k_nMaxShaderOutputCount = 32;
	static constexpr uint32_t k_nMaxShaderResourceCount = 32;
	static constexpr uint32_t k_nMaxShaderPushConstantMemberCount = 32;

	struct ShaderVariable
	{
		char Name[k_nMaxShaderVariableNameLength] = {};
		uint32_t uLocation = 0;
		uint32_t uRegisterIndex = 0;   // Descriptor set (TReg/SReg space) for resources.
		uint32_t uBindingIndex = 0;    // Binding inside that set.
		bool bIsBuiltIn = false;       // True for SV_Position and friends.
		ShaderTypeInfo Type;
	};

	// One descriptor binding the stage reads.
	struct ShaderResourceBinding
	{
		char Name[k_nMaxShaderVariableNameLength] = {};
		ShaderResourceKind eKind = ShaderResourceKind::Unknown;
		uint32_t uDescriptorSet = 0;
		uint32_t uBinding = 0;
		uint32_t uDescriptorCount = 1;   // > 1 for (runtime) arrays.
		uint32_t uElementByteSize = 0;
	};

	// One member of the push-constant block.
	struct ShaderPushConstantMember
	{
		char Name[k_nMaxShaderVariableNameLength] = {};
		uint32_t uByteOffset = 0;
		uint32_t uByteSize = 0;
	};

	// Everything reflection knows about one compiled stage.
	struct ShaderStageReflection
	{
		ShaderStage eStage = ShaderStage::Vertex;
		char EntryPointName[k_nMaxShaderVariableNameLength] = {};

		uint32_t uInputCount = 0;
		uint32_t uOutputCount = 0;
		uint32_t uResourceCount = 0;
		uint32_t uPushConstantMemberCount = 0;
		uint32_t uPushConstantByteSize = 0;

		ShaderVariable Inputs[k_nMaxShaderInputCount];
		ShaderVariable Outputs[k_nMaxShaderOutputCount];
		ShaderResourceBinding Resources[k_nMaxShaderResourceCount];
		ShaderPushConstantMember PushConstantMembers[k_nMaxShaderPushConstantMemberCount];
	};

	// Number of uint32 values a reflection summary occupies when it is handed
	// across the engine boundary (see VspShader_GetStageReflectionSummary).
	static constexpr uint32_t k_nReflectionSummaryValueCount = 6;

	// Fills pOutValues with the summary counters in this order:
	//   [0] input count, [1] output count, [2] resource count,
	//   [3] push-constant member count, [4] push-constant byte size,
	//   [5] reserved (0).
	void BuildReflectionSummary(const ShaderStageReflection& reflection, uint32_t* pOutValues);
}
