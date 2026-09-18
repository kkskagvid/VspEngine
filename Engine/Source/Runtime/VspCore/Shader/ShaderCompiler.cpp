#include "RuntimePCH.h"

#include "Core/Logging/Log.h"
#include "Shader/ShaderCompiler.h"
#include "ShaderOutputWriter.h"

namespace Vsp
{
	static constexpr const char* kLogTag = "ShaderCompiler";

	ShaderCompiler& ShaderCompiler::Get()
	{
		static ShaderCompiler s_Instance;
		return s_Instance;
	}

	bool ShaderCompiler::IsCompilerAvailable(VspString& outErrorText)
	{
		std::string sErrorText;
		const bool bIsAvailable = Hlslcc::Compiler::IsCompilerAvailable(sErrorText);
		if (!bIsAvailable)
		{
			outErrorText = VspString(sErrorText);
			return false;
		}

		outErrorText = nullptr;
		return true;
	}

	bool ShaderCompiler::CompileSource(
		const VspString& sSourceText,
		const VspString& sSourceName,
		VspString& outErrorText)
	{
		return CompileWithOptions(
			std::string(sSourceText.GetData()),
			std::string(sSourceName.GetData()),
			outErrorText);
	}

	bool ShaderCompiler::CompileFile(const VspString& sFilePath, VspString& outErrorText)
	{
		outErrorText = nullptr;

		Hlslcc::ShaderFile shaderFile;
		std::string sErrorText;
		const Hlslcc::HlslccResult eLoadResult = shaderFile.LoadFromFile(
			std::string(sFilePath.GetData()), sErrorText);
		if (eLoadResult != Hlslcc::HlslccResult::Success)
		{
			outErrorText = VspString(sErrorText);
			LOG_ERROR(kLogTag, "Reading '{}' failed: {}", sFilePath.GetData(), sErrorText);
			return false;
		}

		return CompileWithOptions(shaderFile.GetSourceText(), shaderFile.GetDisplayName(), outErrorText);
	}

	bool ShaderCompiler::CompileWithOptions(
		const std::string& sSourceText,
		const std::string& sSourceName,
		VspString& outErrorText)
	{
		outErrorText = nullptr;
		Clear();

		Hlslcc::CompiledShader compiledShader;
		std::string sErrorText;
		const Hlslcc::HlslccResult eCompileResult = Hlslcc::Compiler::CompileSource(
			sSourceText, sSourceName, m_CompileOptions, compiledShader, sErrorText);
		if (eCompileResult != Hlslcc::HlslccResult::Success)
		{
			outErrorText = VspString(sErrorText);
			LOG_ERROR(kLogTag, "Compiling '{}' failed: {}", sSourceName, sErrorText);
			return false;
		}

		m_CompiledShader = std::move(compiledShader);
		m_sSourceName = VspString(sSourceName);
		m_bHasCompiledShader = true;

		const std::string sReflectionJson = Hlslcc::ShaderOutputWriter::BuildReflectionJson(m_CompiledShader);
		m_sReflectionJson = VspString(sReflectionJson);

		LOG_INFO(kLogTag, "Compiled '{}': {} stage(s), {} bytes of reflection.",
			sSourceName, GetCompiledStageCount(), static_cast<uint32>(sReflectionJson.size()));

		for (uint32 uStageIndex = 0; uStageIndex < Hlslcc::k_nShaderStageCount; ++uStageIndex)
		{
			const Hlslcc::CompiledStage* pStage =
				m_CompiledShader.GetDefaultStage(static_cast<Hlslcc::ShaderStage>(uStageIndex));
			if (pStage == nullptr || !pStage->IsValid())
			{
				continue;
			}
			const Hlslcc::CompiledStage& stage = *pStage;

			LOG_INFO(kLogTag, "  {} entry '{}': {} SPIR-V bytes, {} inputs, {} outputs, {} resources, push constants {} bytes.",
				Hlslcc::ToStageName(stage.eStage),
				stage.EntryPointName,
				stage.GetSpirvByteCount(),
				stage.Reflection.uInputCount,
				stage.Reflection.uOutputCount,
				stage.Reflection.uResourceCount,
				stage.Reflection.uPushConstantByteSize);
		}

		return true;
	}

	uint32 ShaderCompiler::GetCompiledStageCount() const
	{
		return m_bHasCompiledShader ? m_CompiledShader.GetCompiledStageCount() : 0u;
	}

	const std::vector<uint32>& ShaderCompiler::GetStageSpirvWords(int32 nStage) const
	{
		static const std::vector<uint32> s_EmptyWords;

		const Hlslcc::CompiledStage* pStage = GetDefaultStage(nStage);
		return (pStage != nullptr) ? pStage->SpirvWords : s_EmptyWords;
	}

	const Hlslcc::CompiledStage* ShaderCompiler::GetDefaultStage(int32 nStage) const
	{
		if (!m_bHasCompiledShader || nStage < 0 || nStage >= static_cast<int32>(Hlslcc::k_nShaderStageCount))
		{
			return nullptr;
		}
		return m_CompiledShader.GetDefaultStage(static_cast<Hlslcc::ShaderStage>(nStage));
	}

	const char* ShaderCompiler::GetStageEntryPointName(int32 nStage) const
	{
		const Hlslcc::CompiledStage* pStage = GetDefaultStage(nStage);
		return (pStage != nullptr) ? pStage->EntryPointName.c_str() : "";
	}

	const Hlslcc::ShaderStageReflection* ShaderCompiler::GetStageReflection(int32 nStage) const
	{
		const Hlslcc::CompiledStage* pStage = GetDefaultStage(nStage);
		return (pStage != nullptr && pStage->IsValid()) ? &pStage->Reflection : nullptr;
	}

	void ShaderCompiler::Clear()
	{
		m_CompiledShader = Hlslcc::CompiledShader();
		m_sReflectionJson = nullptr;
		m_sSourceName = nullptr;
		m_bHasCompiledShader = false;
	}
}