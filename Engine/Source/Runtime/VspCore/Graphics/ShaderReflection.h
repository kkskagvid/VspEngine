#pragma once

#include "Core/Core.h"

namespace Vsp
{
	// Pipeline stages a compiled shader has a module for.
	enum class ShaderStage : uint32
	{
		Vertex = 0,
		Fragment = 1,
	};

	static constexpr uint32 k_nShaderStageCount = 2;

	// -------------------------------------------------------------------------
	// Shader reflection summary
	// -------------------------------------------------------------------------
	// The counters of one compiled stage a pipeline checks itself against, as
	// HLSLCC reports them. The full reflection document lives next to the SPIR-V
	// modules; the runtime only needs these numbers.
	// -------------------------------------------------------------------------
	struct RUNTIME_API ShaderStageReflectionSummary
	{
		uint32 uInputCount = 0;
		uint32 uOutputCount = 0;
		uint32 uResourceCount = 0;
		uint32 uPushConstantByteSize = 0;
	};
}
