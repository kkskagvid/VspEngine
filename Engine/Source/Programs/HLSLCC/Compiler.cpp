#include "Compiler.h"

#include <cstdio>
#include <cstring>

#include "DxcCompilerApi.h"
#include "SpirvBindingAssigner.h"
#include "SpirvReflectionReader.h"

namespace Hlslcc
{
	// -------------------------------------------------------------------------
	// CompiledPass / CompiledVariant / CompiledShader
	// -------------------------------------------------------------------------

	uint32_t CompiledPass::GetCompiledStageCount() const
	{
		uint32_t uCompiledStageCount = 0;
		for (uint32_t uStageIndex = 0; uStageIndex < k_nShaderStageCount; ++uStageIndex)
		{
			uCompiledStageCount += Stages[uStageIndex].IsValid() ? 1u : 0u;
		}
		return uCompiledStageCount;
	}

	bool CompiledVariant::IsValid() const
	{
		for (const CompiledPass& pass : Passes)
		{
			if (pass.IsValid())
			{
				return true;
			}
		}
		return false;
	}

	uint32_t CompiledVariant::GetCompiledStageCount() const
	{
		uint32_t uCompiledStageCount = 0;
		for (const CompiledPass& pass : Passes)
		{
			uCompiledStageCount += pass.GetCompiledStageCount();
		}
		return uCompiledStageCount;
	}

	const CompiledPass* CompiledVariant::GetPass(uint32_t uPassIndex) const
	{
		return (uPassIndex < Passes.size()) ? &Passes[uPassIndex] : nullptr;
	}

	bool CompiledShader::IsValid() const
	{
		for (const CompiledVariant& variant : Variants)
		{
			if (variant.IsValid())
			{
				return true;
			}
		}
		return false;
	}

	uint32_t CompiledShader::GetCompiledStageCount() const
	{
		const CompiledVariant* pDefaultVariant = GetDefaultVariant();
		return (pDefaultVariant != nullptr) ? pDefaultVariant->GetCompiledStageCount() : 0u;
	}

	const CompiledVariant* CompiledShader::GetDefaultVariant() const
	{
		return Variants.empty() ? nullptr : &Variants.front();
	}

	CompiledVariant* CompiledShader::GetDefaultVariant()
	{
		return Variants.empty() ? nullptr : &Variants.front();
	}

	const CompiledStage* CompiledShader::GetDefaultStage(ShaderStage eStage) const
	{
		const CompiledVariant* pDefaultVariant = GetDefaultVariant();
		if (pDefaultVariant == nullptr || pDefaultVariant->Passes.empty())
		{
			return nullptr;
		}
		return &pDefaultVariant->Passes.front().GetStage(eStage);
	}

	// -------------------------------------------------------------------------
	// Compiler
	// -------------------------------------------------------------------------

	bool Compiler::IsCompilerAvailable(std::string& outErrorText)
	{
		return DxcCompilerApi::Get().Initialize(outErrorText) == HlslccResult::Success;
	}

	void Compiler::ApplyEntryPointOverrides(ShaderFile& shaderFile, const CompileOptions& options)
	{
		// A pass that names its own entry points keeps them; the options only fill
		// in the passes that rely on the defaults.
		for (uint32_t uPassIndex = 0; uPassIndex < shaderFile.GetPassCount(); ++uPassIndex)
		{
			const ShaderPass& pass = shaderFile.GetPass(uPassIndex);
			if (pass.VertexEntryPoint == ShaderFile::k_sDefaultVertexEntryPoint && !options.VertexEntryPoint.empty())
			{
				shaderFile.SetEntryPoint(ShaderStage::Vertex, options.VertexEntryPoint);
			}
			if (pass.FragmentEntryPoint == ShaderFile::k_sDefaultFragmentEntryPoint && !options.FragmentEntryPoint.empty())
			{
				shaderFile.SetEntryPoint(ShaderStage::Fragment, options.FragmentEntryPoint);
			}
			break;   // SetEntryPoint already applies to every pass.
		}

		if (shaderFile.GetPassCount() == 0)
		{
			shaderFile.SetEntryPoint(ShaderStage::Vertex, options.VertexEntryPoint);
			shaderFile.SetEntryPoint(ShaderStage::Fragment, options.FragmentEntryPoint);
		}
	}

	HlslccResult Compiler::InspectFile(
		const std::string& sFilePath,
		CompileOptions& options,
		ShaderFile& outShaderFile,
		std::string& outErrorText)
	{
		const HlslccResult eLoadResult = outShaderFile.LoadFromFile(sFilePath, outErrorText);
		if (eLoadResult != HlslccResult::Success)
		{
			return eLoadResult;
		}

		ApplyEntryPointOverrides(outShaderFile, options);

		if (!options.bAllowMissingStages &&
			(!outShaderFile.HasEntryPoint(ShaderStage::Vertex) || !outShaderFile.HasEntryPoint(ShaderStage::Fragment)))
		{
			outErrorText = "the shader file '" + sFilePath + "' does not define both entry points";
			return HlslccResult::FailMissingEntryPoint;
		}

		// The variant space is decided before anything is compiled, so a shader
		// whose keywords do not fit is rejected right away.
		std::vector<ShaderVariantKey> variantKeys;
		if (!BuildShaderVariantKeys(outShaderFile.GetKeywordGroups(), options.UsedKeywordStates, variantKeys, outErrorText))
		{
			return HlslccResult::FailInvalidArgument;
		}
		return HlslccResult::Success;
	}

	HlslccResult Compiler::CompileFile(
		const std::string& sFilePath,
		const CompileOptions& options,
		CompiledShader& outShader,
		std::string& outErrorText)
	{
		CompileOptions resolvedOptions = options;

		ShaderFile shaderFile;
		const HlslccResult eInspectResult = InspectFile(sFilePath, resolvedOptions, shaderFile, outErrorText);
		if (eInspectResult != HlslccResult::Success)
		{
			return eInspectResult;
		}

		return CompileShaderFile(shaderFile, resolvedOptions, outShader, outErrorText);
	}

	HlslccResult Compiler::CompileSource(
		const std::string& sSourceText,
		const std::string& sSourceName,
		const CompileOptions& options,
		CompiledShader& outShader,
		std::string& outErrorText)
	{
		outErrorText.clear();
		if (sSourceText.empty())
		{
			outErrorText = "the shader source is empty";
			return HlslccResult::FailInvalidArgument;
		}

		// Plain HLSL (no Shader block) is accepted as one pass holding the text,
		// which is what the engine's runtime compiler hands over.
		ShaderFile shaderFile;
		if (shaderFile.LoadFromSource(sSourceText, sSourceName, outErrorText) != HlslccResult::Success)
		{
			return HlslccResult::FailInvalidArgument;
		}
		ApplyEntryPointOverrides(shaderFile, options);

		return CompileShaderFile(shaderFile, options, outShader, outErrorText);
	}

	// -------------------------------------------------------------------------
	// The compile flow shared by CompileFile and CompileSource
	// -------------------------------------------------------------------------

	HlslccResult Compiler::CompileShaderFile(
		const ShaderFile& shaderFile,
		const CompileOptions& options,
		CompiledShader& outShader,
		std::string& outErrorText)
	{
		outErrorText.clear();
		outShader = CompiledShader();
		outShader.SourceName = shaderFile.GetDisplayName();
		outShader.ShaderName = shaderFile.GetShaderName();
		outShader.uRenderQueue = shaderFile.GetRenderQueue();
		outShader.Properties = shaderFile.GetProperties();
		outShader.KeywordGroups = shaderFile.GetKeywordGroups();

		if (shaderFile.GetPassCount() == 0)
		{
			outErrorText = "the shader file '" + shaderFile.GetDisplayName() + "' holds no Pass block";
			return HlslccResult::FailMissingEntryPoint;
		}

		// ---- Which variants the build keeps ----
		std::vector<ShaderVariantKey> variantKeys;
		if (!BuildShaderVariantKeys(shaderFile.GetKeywordGroups(), options.UsedKeywordStates, variantKeys, outErrorText))
		{
			return HlslccResult::FailInvalidArgument;
		}

		// The shader's own directory resolves its #include directives.
		std::vector<std::string> sIncludeDirectories = options.IncludeDirectories;
		sIncludeDirectories.push_back(shaderFile.GetSourceDirectory());

		// ---- Compile every pass of every kept variant ----
		uint32_t uVariantIndex = 0;
		for (const ShaderVariantKey& variantKey : variantKeys)
		{
			CompiledVariant compiledVariant;
			compiledVariant.Key = variantKey;
			compiledVariant.uVariantIndex = uVariantIndex;
			++uVariantIndex;

			for (uint32_t uPassIndex = 0; uPassIndex < shaderFile.GetPassCount(); ++uPassIndex)
			{
				const ShaderPass& pass = shaderFile.GetPass(uPassIndex);

				CompiledPass compiledPass;
				if (!CompilePass(pass, variantKey, options, sIncludeDirectories, outShader.SourceName,
					uPassIndex, compiledPass, outErrorText))
				{
					return HlslccResult::FailCompilation;
				}
				compiledVariant.Passes.push_back(std::move(compiledPass));
			}

			outShader.Variants.push_back(std::move(compiledVariant));
		}

		if (!outShader.IsValid())
		{
			outErrorText = "no pass of '" + shaderFile.GetDisplayName() + "' produced a SPIR-V module";
			return HlslccResult::FailMissingEntryPoint;
		}
		return HlslccResult::Success;
	}

	// True when the pass names its own descriptor bindings, in which case the
	// engine leaves them alone.
	bool Compiler::DoesPassSpecifyBindings(const ShaderPass& pass)
	{
		return ShaderSourceInjector::SourceTextSpecifiesBindings(pass.SourceText);
	}

	// Compiles both stages of one pass for one variant. The two stages are compiled
	// first and read back afterwards, because the engine's binding rules number the
	// resources of the WHOLE pass: the vertex and the fragment stage share one
	// descriptor set, so they have to agree on the numbers.
	bool Compiler::CompilePass(
		const ShaderPass& pass,
		const ShaderVariantKey& variantKey,
		const CompileOptions& options,
		const std::vector<std::string>& sIncludeDirectories,
		const std::string& sSourceName,
		uint32_t uPassIndex,
		CompiledPass& outPass,
		std::string& outErrorText)
	{
		// The keywords of this variant are defined in front of the pass, so the
		// HLSL reads them like any other preprocessor definition.
		std::string sPassSource;
		for (const std::string& sDefine : variantKey.Defines)
		{
			sPassSource += "#define " + sDefine + " 1\n";
		}
		sPassSource += pass.SourceText;

		InjectionOptions injectionOptions;
		injectionOptions.bInjectVulkanNamespace = options.bInjectVulkanNamespace;
		injectionOptions.bInjectEngineAttributeShorthands = options.bInjectEngineAttributeShorthands;

		InjectionResult injectionResult;
		std::string sInjectionErrorText;
		if (ShaderSourceInjector::Inject(sPassSource, injectionOptions, injectionResult, sInjectionErrorText) !=
			HlslccResult::Success)
		{
			outErrorText = "shader source preparation failed: " + sInjectionErrorText;
			return false;
		}

		std::vector<std::string> sPassIncludeDirectories = sIncludeDirectories;
		if (injectionResult.bVulkanNamespaceHeaderIncluded &&
			!injectionResult.VulkanNamespaceIncludeDirectory.empty())
		{
			sPassIncludeDirectories.push_back(injectionResult.VulkanNamespaceIncludeDirectory);
		}

		outPass.uPassIndex = uPassIndex;
		std::memcpy(outPass.Name, pass.Name, sizeof(outPass.Name));
		outPass.VertexEntryPoint = pass.VertexEntryPoint;
		outPass.FragmentEntryPoint = pass.FragmentEntryPoint;

		const std::string sVariantText = variantKey.IsDefault()
			? std::string("(default variant)")
			: ("(variant " + variantKey.KeyText + ")");

		// ---- 1. Compile every stage the pass defines ----
		for (uint32_t uStageIndex = 0; uStageIndex < k_nShaderStageCount; ++uStageIndex)
		{
			const ShaderStage eStage = static_cast<ShaderStage>(uStageIndex);
			CompiledStage& outStage = outPass.Stages[uStageIndex];
			outStage.eStage = eStage;
			outStage.EntryPointName =
				(eStage == ShaderStage::Vertex) ? pass.VertexEntryPoint : pass.FragmentEntryPoint;

			if (!ShaderFile::SourceDeclaresFunction(pass.SourceText, outStage.EntryPointName))
			{
				if (!options.bAllowMissingStages)
				{
					outErrorText = "pass '" + std::string(pass.Name) + "' of '" + sSourceName +
						"' does not define the " + ToStageName(eStage) + " entry point '" +
						outStage.EntryPointName + "'";
					return false;
				}
				continue;
			}

			CompileRequest request;
			request.SourceText = injectionResult.SourceText;
			request.SourceName = sSourceName;
			request.EntryPoint = outStage.EntryPointName;
			request.eStage = eStage;
			request.TargetEnvironment = options.TargetEnvironment;
			request.IncludeDirectories = sPassIncludeDirectories;
			request.Defines = options.Defines;
			request.bDebugInfo = options.bDebugInfo;

			CompileOutput output;
			std::string sCompileErrorText;
			if (DxcCompilerApi::Get().CompileToSpirv(request, output, sCompileErrorText) != HlslccResult::Success)
			{
				outErrorText = std::string(ToStageName(eStage)) + " entry point '" + outStage.EntryPointName +
					"' of pass '" + std::string(pass.Name) + "' " + sVariantText +
					" failed to compile:\n" + sCompileErrorText;
				return false;
			}

			outStage.SpirvWords = std::move(output.SpirvWords);
			outStage.Diagnostics = std::move(output.Diagnostics);
			outStage.bSourceSpecifiesBindings = output.bSourceSpecifiesBindings;
		}

		// ---- 2. Let the engine decide the bindings ----
		if (!AssignPassBindings(pass, options, outPass, outErrorText))
		{
			return false;
		}

		// ---- 3. Read the finished modules back for reflection ----
		for (uint32_t uStageIndex = 0; uStageIndex < k_nShaderStageCount; ++uStageIndex)
		{
			const ShaderStage eStage = static_cast<ShaderStage>(uStageIndex);
			CompiledStage& outStage = outPass.Stages[uStageIndex];
			if (!outStage.IsValid())
			{
				continue;
			}

			std::string sReflectionErrorText;
			if (SpirvReflectionReader::Read(outStage.SpirvWords, eStage, outStage.Reflection, sReflectionErrorText) !=
				HlslccResult::Success)
			{
				outErrorText = std::string("reflection of the ") + ToStageName(eStage) + " stage failed: " +
					sReflectionErrorText;
				return false;
			}
			outStage.Reflection.eStage = eStage;
			if (outStage.Reflection.EntryPointName[0] == '\0')
			{
				std::snprintf(outStage.Reflection.EntryPointName, k_nMaxShaderVariableNameLength, "%s",
					outStage.EntryPointName.c_str());
			}
		}

		return true;
	}

	// Applies the engine's binding rules to a compiled pass.
	bool Compiler::AssignPassBindings(
		const ShaderPass& pass,
		const CompileOptions& options,
		CompiledPass& outPass,
		std::string& outErrorText)
	{
		// A shader that numbers its own resources keeps them, whether it wrote the
		// numbers in the Pass block or in a file the Pass includes. The stages
		// carry what the compiler saw while it resolved the includes, so a
		// binding inside an include file is found too.
		bool bPassSpecifiesBindings = DoesPassSpecifyBindings(pass);
		for (const CompiledStage& stage : outPass.Stages)
		{
			bPassSpecifiesBindings = bPassSpecifiesBindings || stage.bSourceSpecifiesBindings;
		}

		if (!options.bAutoAssignBindings || bPassSpecifiesBindings)
		{
			return true;   // The shader manages its own bindings.
		}

		std::string sBindingErrorText;
		if (SpirvBindingAssigner::AssignPassBindings(
			outPass.Stages[0].SpirvWords, outPass.Stages[1].SpirvWords, sBindingErrorText) != HlslccResult::Success)
		{
			outErrorText = "assigning the engine's bindings to pass '" + std::string(pass.Name) +
				"' failed: " + sBindingErrorText;
			return false;
		}
		return true;
	}
}