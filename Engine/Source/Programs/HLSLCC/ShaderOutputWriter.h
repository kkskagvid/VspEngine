#pragma once

#include <string>

#include "Compiler.h"
#include "Result.h"
#include "ShaderDefinition.h"
#include "ShaderReflection.h"

namespace Hlslcc
{
	// -------------------------------------------------------------------------
	// ShaderOutputWriter
	// -------------------------------------------------------------------------
	// Writes what a compilation produced:
	//
	//   <name>.<stage>.spv                 the DEFAULT variant's modules
	//   <name>.<variant>.<stage>.spv       the other variants' modules
	//   <name>.shader.json                 the manifest the engine loads: the
	//                                      queue, the properties, the keyword
	//                                      groups and where every variant's
	//                                      modules live
	//   <name>.reflection.json             the full reflection of the default
	//                                      variant, for tools and humans
	//
	// Nothing throws; every failure is a HlslccResult and a message.
	// -------------------------------------------------------------------------
	class ShaderOutputWriter
	{
	public:
		// Suffix one variant contributes to a file name: empty for the default
		// variant, the enabled keyword names joined by '+' otherwise.
		static std::string BuildVariantFileSuffix(const CompiledVariant& variant);

		// Path of the SPIR-V file of one stage, e.g.
		// "<directory>/<baseName>[.<pass>][.<variant>].vert.spv".
		static std::string BuildStageSpirvPath(
			const std::string& sOutputDirectory,
			const std::string& sBaseName,
			const CompiledPass& pass,
			const CompiledVariant& variant,
			uint32_t uPassCount,
			ShaderStage eStage);

		// Path of the manifest the engine loads.
		static std::string BuildManifestPath(const std::string& sOutputDirectory, const std::string& sBaseName);

		// Path of the reflection document (default variant, every stage).
		static std::string BuildReflectionPath(const std::string& sOutputDirectory, const std::string& sBaseName);

		// Base name of a shader path (".../Triangle2D.vsf" -> "Triangle2D").
		static std::string ExtractBaseName(const std::string& sFilePath);

		// Directory part of a path; "." when it has none.
		static std::string ExtractDirectory(const std::string& sFilePath);

		// Writes the SPIR-V module of one stage.
		static HlslccResult WriteStageSpirv(
			const std::string& sFilePath,
			const CompiledStage& stage,
			std::string& outErrorText);

		// Renders the manifest: everything the engine needs to load the shader.
		static std::string BuildShaderManifestJson(
			const CompiledShader& shader,
			const std::string& sBaseName);

		// Writes the manifest.
		static HlslccResult WriteShaderManifest(
			const std::string& sFilePath,
			const CompiledShader& shader,
			const std::string& sBaseName,
			std::string& outErrorText);

		// Renders the reflection of the default variant as JSON.
		static std::string BuildReflectionJson(const CompiledShader& shader);

		// Writes the reflection document.
		static HlslccResult WriteReflectionJson(
			const std::string& sFilePath,
			const CompiledShader& shader,
			std::string& outErrorText);

		// Renders the console summary of a compilation.
		static std::string BuildConsoleSummary(const CompiledShader& shader);
	};
}
