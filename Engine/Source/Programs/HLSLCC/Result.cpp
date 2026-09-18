#include "Result.h"

namespace Hlslcc
{
	const char* ToText(HlslccResult eResult)
	{
		switch (eResult)
		{
		case HlslccResult::Success:                  return "success";
		case HlslccResult::FailCannotOpenFile:       return "the shader source file could not be read";
		case HlslccResult::FailCannotLoadCompiler:   return "the DirectX Shader Compiler (dxcompiler.dll) could not be loaded";
		case HlslccResult::FailMissingEntryPoint:    return "the shader file does not define the requested entry point";
		case HlslccResult::FailCompilation:          return "the HLSL compiler rejected the shader source";
		case HlslccResult::FailReflection:           return "the produced SPIR-V could not be read for reflection";
		case HlslccResult::FailInvalidArgument:      return "an argument the caller passed is unusable";
		case HlslccResult::FailWriteOutput:          return "an output file could not be written";
		default:                                     return "unknown failure";
		}
	}
}
