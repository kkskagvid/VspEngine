#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Result.h"
#include "ShaderDefinition.h"
#include "ShaderReflection.h"
#include "ShaderSourceInjector.h"
#include "VspShaderFile.h"

namespace Hlslcc
{
	// -------------------------------------------------------------------------
	// Compiler
	// -------------------------------------------------------------------------
	// The HLSL cross compiler. It takes ONE .vsf shader file - the properties, the
	// shader settings and every Pass with its HLSL in one file - and produces, for
	// every variant the build keeps, one SPIR-V module per stage of every pass.
	//
	// The compiler is the whole product: the command line tool HLSLCC.exe and the
	// engine's runtime shader compiler are both thin wrappers around it, so a
	// shader compiles to exactly the same result either way.
	//
	// Nothing throws: every entry point reports through HlslccResult and a
	// message the caller can log.
	// -------------------------------------------------------------------------

	// How one shader file is turned into SPIR-V.
	struct CompileOptions
	{
		// Entry points used by passes that do not name them with
		// "#pragma vertex" / "#pragma fragment".
		std::string VertexEntryPoint = ShaderFile::k_sDefaultVertexEntryPoint;
		std::string FragmentEntryPoint = ShaderFile::k_sDefaultFragmentEntryPoint;

		// Vulkan environment the SPIR-V is generated for.
		std::string TargetEnvironment = "vulkan1.3";

		// Extra include directories handed to the compiler (the directory that
		// provides the Vulkan HLSL namespace and the shader's own directory are
		// added automatically).
		std::vector<std::string> IncludeDirectories;

		// Preprocessor definitions ("NAME" or "NAME=VALUE").
		std::vector<std::string> Defines;

		// Keyword states the build actually uses. A strippable keyword group
		// (variant / variant_local) keeps only these; an empty list keeps just the
		// group's default state.
		std::vector<std::string> UsedKeywordStates;

		// Inject the Vulkan HLSL namespace / the engine attribute shorthands.
		bool bInjectVulkanNamespace = true;
		bool bInjectEngineAttributeShorthands = true;

		// Compile without optimisation and keep debug information.
		bool bDebugInfo = false;

		// Only the stages the file actually defines are compiled; a missing
		// entry point is a failure when this is false.
		bool bAllowMissingStages = false;
	};

	// One compiled stage.
	struct CompiledStage
	{
		ShaderStage eStage = ShaderStage::Vertex;
		std::string EntryPointName;
		std::vector<uint32_t> SpirvWords;
		ShaderStageReflection Reflection;
		std::string Diagnostics;

		// True once a non-empty SPIR-V module was produced.
		bool IsValid() const { return SpirvWords.size() >= 5; }

		uint32_t GetSpirvByteCount() const { return static_cast<uint32_t>(SpirvWords.size() * sizeof(uint32_t)); }
	};

	// One compiled pass: the modules of both stages.
	struct CompiledPass
	{
		uint32_t uPassIndex = 0;
		char Name[k_nMaxShaderKeywordLength] = {};
		std::string VertexEntryPoint;
		std::string FragmentEntryPoint;
		CompiledStage Stages[k_nShaderStageCount];

		bool IsValid() const { return Stages[0].IsValid() || Stages[1].IsValid(); }
		uint32_t GetCompiledStageCount() const;
		CompiledStage& GetStage(ShaderStage eStage) { return Stages[static_cast<uint32_t>(eStage)]; }
		const CompiledStage& GetStage(ShaderStage eStage) const { return Stages[static_cast<uint32_t>(eStage)]; }
	};

	// One compiled variant: every pass, compiled with its keywords defined.
	struct CompiledVariant
	{
		ShaderVariantKey Key;

		// Index of the variant inside the shader (0 = the default variant).
		uint32_t uVariantIndex = 0;

		std::vector<CompiledPass> Passes;

		// True when the variant defines no keyword at all.
		bool IsDefault() const { return Key.IsDefault(); }

		bool IsValid() const;
		uint32_t GetCompiledStageCount() const;
		const CompiledPass* GetPass(uint32_t uPassIndex) const;
	};

	// Everything one shader file compiled to.
	struct CompiledShader
	{
		std::string SourceName;
		std::string ShaderName;
		uint32_t uRenderQueue = 2000;
		std::vector<ShaderProperty> Properties;
		std::vector<ShaderKeywordGroup> KeywordGroups;
		std::vector<CompiledVariant> Variants;

		// False when no variant produced a module.
		bool IsValid() const;
		uint32_t GetCompiledStageCount() const;

		// The variant a material with no keyword resolves to: the first one.
		const CompiledVariant* GetDefaultVariant() const;
		CompiledVariant* GetDefaultVariant();

		// Convenience for callers that work on the default variant and its first
		// pass (the engine's runtime compiler, the reflection document).
		const CompiledStage* GetDefaultStage(ShaderStage eStage) const;
	};

	class Compiler
	{
	public:
		// Compiles every pass, for every variant the build keeps.
		static HlslccResult CompileFile(
			const std::string& sFilePath,
			const CompileOptions& options,
			CompiledShader& outShader,
			std::string& outErrorText);

		// Compiles shader source that is already in memory (the engine does this
		// for game shaders that ship as text).
		static HlslccResult CompileSource(
			const std::string& sSourceText,
			const std::string& sSourceName,
			const CompileOptions& options,
			CompiledShader& outShader,
			std::string& outErrorText);

		// Reads the source of a file and reports what it declares without
		// compiling anything.
		static HlslccResult InspectFile(
			const std::string& sFilePath,
			CompileOptions& options,
			ShaderFile& outShaderFile,
			std::string& outErrorText);

		// True when the machine can compile HLSL at all (dxcompiler.dll found).
		static bool IsCompilerAvailable(std::string& outErrorText);

		// Applies the entry point overrides of the options to every pass.
		static void ApplyEntryPointOverrides(ShaderFile& shaderFile, const CompileOptions& options);

	private:
		// The compile flow both entry points share: enumerate the variants the
		// build keeps and compile every pass of each of them.
		static HlslccResult CompileShaderFile(
			const ShaderFile& shaderFile,
			const CompileOptions& options,
			CompiledShader& outShader,
			std::string& outErrorText);

		// Compiles both stages of one pass for one variant.
		static bool CompilePass(
			const ShaderPass& pass,
			const ShaderVariantKey& variantKey,
			const CompileOptions& options,
			const std::vector<std::string>& sIncludeDirectories,
			const std::string& sSourceName,
			uint32_t uPassIndex,
			CompiledPass& outPass,
			std::string& outErrorText);
	};
}
