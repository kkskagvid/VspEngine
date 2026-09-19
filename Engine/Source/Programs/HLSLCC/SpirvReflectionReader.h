#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Result.h"
#include "ShaderReflection.h"

namespace Hlslcc
{
	// -------------------------------------------------------------------------
	// SpirvReflectionReader
	// -------------------------------------------------------------------------
	// Reads a compiled SPIR-V module and fills the ShaderStageReflection of the
	// stage it was compiled for: entry point, interface variables with their
	// locations and types, descriptor bindings with set/binding/kind, and the
	// member layout of the push-constant block.
	//
	// The reader is a plain forward walk over the module's instructions, so it
	// needs no SPIR-V toolchain at runtime and never allocates beyond the tables
	// it builds while reading. Nothing throws: an unreadable module is reported
	// through a HlslccResult and a message.
	// -------------------------------------------------------------------------
	class SpirvReflectionReader
	{
	public:
		// Fills outReflection from one SPIR-V module. eStage selects which
		// OpEntryPoint of the module is described.
		static HlslccResult Read(
			const std::vector<uint32_t>& spirvWords,
			ShaderStage eStage,
			ShaderStageReflection& outReflection,
			std::string& outErrorText);

		// Lists the descriptor variables of a module in declaration order, with the
		// words that hold their descriptor set and binding numbers. The compiler
		// uses it to apply the engine's binding rules before it reads the module
		// back for reflection.
		static HlslccResult CollectResources(
			const std::vector<uint32_t>& spirvWords,
			std::vector<SpirvResourceVariable>& outResources,
			std::string& outErrorText);
	};
}
