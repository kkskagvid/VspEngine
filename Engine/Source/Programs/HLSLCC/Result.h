#pragma once

namespace Hlslcc
{
	// -------------------------------------------------------------------------
	// HlslccResult
	// -------------------------------------------------------------------------
	// Every entry point of the compiler reports failure through this code - the
	// compiler never throws and never uses C++ exceptions. Callers turn it into
	// a message with ToText().
	// -------------------------------------------------------------------------
	enum class HlslccResult
	{
		Success = 0,

		// The shader source file could not be read.
		FailCannotOpenFile,

		// The DirectX Shader Compiler (dxcompiler.dll) could not be located or
		// loaded, so no HLSL can be compiled on this machine.
		FailCannotLoadCompiler,

		// The shader file does not define the entry point of a requested stage.
		FailMissingEntryPoint,

		// The HLSL compiler rejected the source.
		FailCompilation,

		// The produced SPIR-V could not be read back for reflection.
		FailReflection,

		// A parameter the caller passed is unusable (empty path, unknown stage).
		FailInvalidArgument,

		// An output file could not be written.
		FailWriteOutput,
	};

	// Human-readable text of a result code (never null).
	const char* ToText(HlslccResult eResult);
}
