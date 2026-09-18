#pragma once

#include <string>

#include "Compiler.h"
#include "Core/Core.h"
#include "Core/String/VspString.h"
#include "Result.h"
#include "ShaderReflection.h"

namespace Vsp
{
	// -------------------------------------------------------------------------
	// ShaderCompiler
	// -------------------------------------------------------------------------
	// The engine's runtime front end for HLSLCC: it compiles HLSL source (a file
	// on disk or text the game holds itself) into one SPIR-V module per stage and
	// keeps the result so the managed side can pick it up stage by stage.
	//
	// It is the same compiler the HLSLCC.exe build tool runs, so a shader
	// compiled at runtime is byte-identical to the one compiled offline.
	//
	// All functions report failure through return values, log the reason through
	// the Log module and never throw.
	// -------------------------------------------------------------------------
	class RUNTIME_API ShaderCompiler
	{
	public:
		static ShaderCompiler& Get();

		// Compiles HLSL text both entry points live in.
		bool CompileSource(const VspString& sSourceText, const VspString& sSourceName, VspString& outErrorText);

		// Reads the file and compiles it.
		bool CompileFile(const VspString& sFilePath, VspString& outErrorText);

		// True while a compiled shader is held.
		bool HasCompiledShader() const { return m_bHasCompiledShader; }

		// Number of stages the last compilation produced (0..2).
		uint32 GetCompiledStageCount() const;

		// True when the machine can compile HLSL at all.
		static bool IsCompilerAvailable(VspString& outErrorText);

		// -------- Results of the last compilation --------
		// SPIR-V module of one stage; empty when that stage was not compiled.
		const std::vector<uint32>& GetStageSpirvWords(int32 nStage) const;

		// Entry point the stage was compiled from.
		const char* GetStageEntryPointName(int32 nStage) const;

		// Reflection of one stage; nullptr when the stage was not compiled.
		const Hlslcc::ShaderStageReflection* GetStageReflection(int32 nStage) const;

		// Reflection of every compiled stage as a JSON document.
		const VspString& GetReflectionJson() const { return m_sReflectionJson; }

		// Name of the source the current result came from.
		const VspString& GetSourceName() const { return m_sSourceName; }

		// Compile options applied to every request (entry points, target
		// environment, injection switches, extra include directories).
		Hlslcc::CompileOptions& GetMutableCompileOptions() { return m_CompileOptions; }
		const Hlslcc::CompileOptions& GetCompileOptions() const { return m_CompileOptions; }

		// Releases the compiled result.
		void Clear();

	private:
		ShaderCompiler() = default;

		bool CompileWithOptions(
			const std::string& sSourceText,
			const std::string& sSourceName,
			VspString& outErrorText);

		// Stage of the default variant's first pass.
		const Hlslcc::CompiledStage* GetDefaultStage(int32 nStage) const;

		Hlslcc::CompileOptions m_CompileOptions;
		Hlslcc::CompiledShader m_CompiledShader;
		VspString m_sReflectionJson;
		VspString m_sSourceName;
		bool m_bHasCompiledShader = false;
	};
}
