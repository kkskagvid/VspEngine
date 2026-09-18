#include "CommandLine.h"

#include <cstdio>
#include <cstring>

#include "DxcCompilerApi.h"
#include "FilePathUtility.h"
#include "ShaderOutputWriter.h"

namespace Hlslcc
{
	namespace
	{
		// True when the argument is exactly the given option.
		bool IsOption(const char* pArgument, const char* pOptionName)
		{
			return std::strcmp(pArgument, pOptionName) == 0;
		}
	}

	void CommandLine::PrintUsage()
	{
		std::printf(
			"HLSLCC - compiles one .vsf shader file into SPIR-V.\n"
			"\n"
			"usage: HLSLCC <shader.vsf> [options]\n"
			"\n"
			"  --output-directory <dir>   directory the .spv/.json files go to\n"
			"                             (default: the directory of the shader file)\n"
			"  --used-variant <state>     keyword state the build uses; repeatable\n"
			"                             (strippable groups keep only these states)\n"
			"  --vertex-entry <name>      entry point for passes without a pragma\n"
			"                             (default: PassVertex)\n"
			"  --fragment-entry <name>    entry point for passes without a pragma\n"
			"                             (default: PassFragment)\n"
			"  --target-env <env>         SPIR-V target environment (default: vulkan1.3)\n"
			"  -I <dir>                   extra include directory\n"
			"  -D <name>[=value]          preprocessor definition\n"
			"  --no-vulkan-namespace      keep the Vulkan HLSL namespace out of the source\n"
			"  --no-attribute-shorthands  keep the VSP_VK_* shorthands out of the source\n"
			"  --reflection-only          write only the manifest and reflection documents\n"
			"  --debug-info               compile unoptimised and keep debug information\n"
			"  --quiet                    report failures only\n"
			"  --help                     print this text\n"
			"\n"
			"A .vsf file holds the Properties block, the Shader block with its queue and\n"
			"keyword groups, and the Pass blocks with the HLSL. The entry points are named\n"
			"with '#pragma vertex <name>' and '#pragma fragment <name>'.\n"
			"\n"
			"outputs: <name>[.<pass>][.<variant>].vert.spv, the same for .frag.spv,\n"
			"         <name>.shader.json (the manifest the engine loads) and\n"
			"         <name>.reflection.json\n");
	}

	CommandLineOptions CommandLine::Parse(int32_t nArgumentCount, char** pArguments)
	{
		CommandLineOptions options;

		for (int32_t nArgumentIndex = 1; nArgumentIndex < nArgumentCount; ++nArgumentIndex)
		{
			const char* pArgument = pArguments[nArgumentIndex];
			if (pArgument == nullptr)
			{
				continue;
			}

			// Reads the value of an option that takes one.
			auto ReadValue = [&](const char* pOptionName, std::string& outValue) -> bool
			{
				if (nArgumentIndex + 1 >= nArgumentCount)
				{
					options.ParseErrorText = std::string("option '") + pOptionName + "' needs a value";
					return false;
				}
				++nArgumentIndex;
				outValue = pArguments[nArgumentIndex];
				return true;
			};

			if (IsOption(pArgument, "--help") || IsOption(pArgument, "-h"))
			{
				options.bShowHelp = true;
				return options;
			}
			else if (IsOption(pArgument, "--quiet"))
			{
				options.bQuiet = true;
			}
			else if (IsOption(pArgument, "--reflection-only"))
			{
				options.bReflectionOnly = true;
			}
			else if (IsOption(pArgument, "--debug-info"))
			{
				options.CompileOptions.bDebugInfo = true;
			}
			else if (IsOption(pArgument, "--no-vulkan-namespace"))
			{
				options.CompileOptions.bInjectVulkanNamespace = false;
			}
			else if (IsOption(pArgument, "--no-attribute-shorthands"))
			{
				options.CompileOptions.bInjectEngineAttributeShorthands = false;
			}
			else if (IsOption(pArgument, "--used-variant"))
			{
				std::string sKeywordState;
				if (!ReadValue(pArgument, sKeywordState)) return options;
				options.CompileOptions.UsedKeywordStates.push_back(sKeywordState);
			}
			else if (IsOption(pArgument, "--output-directory"))
			{
				if (!ReadValue(pArgument, options.OutputDirectory)) return options;
			}
			else if (IsOption(pArgument, "--vertex-entry"))
			{
				if (!ReadValue(pArgument, options.CompileOptions.VertexEntryPoint)) return options;
			}
			else if (IsOption(pArgument, "--fragment-entry"))
			{
				if (!ReadValue(pArgument, options.CompileOptions.FragmentEntryPoint)) return options;
			}
			else if (IsOption(pArgument, "--target-env"))
			{
				if (!ReadValue(pArgument, options.CompileOptions.TargetEnvironment)) return options;
			}
			else if (IsOption(pArgument, "-I"))
			{
				std::string sIncludeDirectory;
				if (!ReadValue(pArgument, sIncludeDirectory)) return options;
				options.CompileOptions.IncludeDirectories.push_back(sIncludeDirectory);
			}
			else if (IsOption(pArgument, "-D"))
			{
				std::string sDefine;
				if (!ReadValue(pArgument, sDefine)) return options;
				options.CompileOptions.Defines.push_back(sDefine);
			}
			else if (pArgument[0] == '-')
			{
				options.ParseErrorText = std::string("unknown option '") + pArgument + "'";
				return options;
			}
			else if (options.ShaderFilePath.empty())
			{
				options.ShaderFilePath = pArgument;
			}
			else
			{
				options.ParseErrorText = "only one shader file can be compiled per run";
				return options;
			}
		}

		if (!options.bShowHelp && options.ShaderFilePath.empty())
		{
			options.ParseErrorText = "no shader file was given";
		}
		return options;
	}

	int32_t CommandLine::Run(const CommandLineOptions& options)
	{
		if (!options.ParseErrorText.empty())
		{
			std::fprintf(stderr, "HLSLCC: %s\n\n", options.ParseErrorText.c_str());
			PrintUsage();
			return 2;
		}

		// The compiler is loaded before anything else so a missing dxcompiler.dll
		// is reported as such instead of as a shader problem.
		std::string sCompilerErrorText;
		if (!Compiler::IsCompilerAvailable(sCompilerErrorText))
		{
			std::fprintf(stderr, "HLSLCC: %s\n", sCompilerErrorText.c_str());
			return 3;
		}

		// The entry points and the keyword groups live in the file, so they are
		// known before compiling.
		CompileOptions resolvedOptions = options.CompileOptions;
		ShaderFile shaderFile;
		std::string sErrorText;
		const HlslccResult eInspectResult =
			Compiler::InspectFile(options.ShaderFilePath, resolvedOptions, shaderFile, sErrorText);
		if (eInspectResult != HlslccResult::Success)
		{
			std::fprintf(stderr, "HLSLCC: %s\n", sErrorText.c_str());
			return 4;
		}

		if (!options.bQuiet)
		{
			std::printf("HLSLCC: %s\n", options.ShaderFilePath.c_str());
			std::printf("  shader name          : %s\n", shaderFile.GetShaderName().c_str());
			std::printf("  render queue         : %u\n", shaderFile.GetRenderQueue());
			std::printf("  passes               : %u\n", shaderFile.GetPassCount());
			for (uint32_t uPassIndex = 0; uPassIndex < shaderFile.GetPassCount(); ++uPassIndex)
			{
				const ShaderPass& pass = shaderFile.GetPass(uPassIndex);
				std::printf("    pass '%s': vertex '%s', fragment '%s'\n",
					pass.Name, pass.VertexEntryPoint.c_str(), pass.FragmentEntryPoint.c_str());
			}
			std::printf("  target environment   : %s\n", resolvedOptions.TargetEnvironment.c_str());
		}

		CompiledShader compiledShader;
		const HlslccResult eCompileResult = Compiler::CompileFile(
			options.ShaderFilePath, resolvedOptions, compiledShader, sErrorText);
		if (eCompileResult != HlslccResult::Success)
		{
			std::fprintf(stderr, "HLSLCC: %s\n", sErrorText.c_str());
			return 5;
		}

		const std::string sOutputDirectory = options.OutputDirectory.empty()
			? ShaderOutputWriter::ExtractDirectory(options.ShaderFilePath)
			: options.OutputDirectory;
		const std::string sBaseName = ShaderOutputWriter::ExtractBaseName(options.ShaderFilePath);

		// The output directory is created on demand, so a build system can point
		// HLSLCC at a directory that does not exist yet.
		if (!FilePathUtility::EnsureDirectoryExists(sOutputDirectory))
		{
			std::fprintf(stderr, "HLSLCC: cannot create the output directory '%s'\n", sOutputDirectory.c_str());
			return 6;
		}

		if (!options.bQuiet)
		{
			std::printf("%s", ShaderOutputWriter::BuildConsoleSummary(compiledShader).c_str());
		}

		const uint32_t uPassCount = compiledShader.GetDefaultVariant() != nullptr
			? static_cast<uint32_t>(compiledShader.GetDefaultVariant()->Passes.size())
			: 0u;

		// ---- SPIR-V binaries, one per pass and stage of every kept variant ----
		if (!options.bReflectionOnly)
		{
			for (const CompiledVariant& variant : compiledShader.Variants)
			{
				for (const CompiledPass& pass : variant.Passes)
				{
					for (uint32_t uStageIndex = 0; uStageIndex < k_nShaderStageCount; ++uStageIndex)
					{
						const CompiledStage& stage = pass.Stages[uStageIndex];
						if (!stage.IsValid())
						{
							continue;
						}

						const std::string sSpirvPath = ShaderOutputWriter::BuildStageSpirvPath(
							sOutputDirectory, sBaseName, pass, variant, uPassCount, stage.eStage);
						if (ShaderOutputWriter::WriteStageSpirv(sSpirvPath, stage, sErrorText) !=
							HlslccResult::Success)
						{
							std::fprintf(stderr, "HLSLCC: %s\n", sErrorText.c_str());
							return 6;
						}

						if (!options.bQuiet)
						{
							std::printf("  wrote %s (%u bytes)\n", sSpirvPath.c_str(), stage.GetSpirvByteCount());
						}
					}
				}
			}
		}

		// ---- The manifest the engine loads ----
		const std::string sManifestPath = ShaderOutputWriter::BuildManifestPath(sOutputDirectory, sBaseName);
		if (ShaderOutputWriter::WriteShaderManifest(sManifestPath, compiledShader, sBaseName, sErrorText) !=
			HlslccResult::Success)
		{
			std::fprintf(stderr, "HLSLCC: %s\n", sErrorText.c_str());
			return 7;
		}
		if (!options.bQuiet)
		{
			std::printf("  wrote %s\n", sManifestPath.c_str());
		}

		// ---- Full reflection of the default variant ----
		const std::string sReflectionPath = ShaderOutputWriter::BuildReflectionPath(sOutputDirectory, sBaseName);
		if (ShaderOutputWriter::WriteReflectionJson(sReflectionPath, compiledShader, sErrorText) !=
			HlslccResult::Success)
		{
			std::fprintf(stderr, "HLSLCC: %s\n", sErrorText.c_str());
			return 8;
		}
		if (!options.bQuiet)
		{
			std::printf("  wrote %s\n", sReflectionPath.c_str());
		}
		return 0;
	}
}
