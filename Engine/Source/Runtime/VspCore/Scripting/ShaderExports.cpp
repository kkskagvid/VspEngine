#include "RuntimePCH.h"

#include <cstring>
#include <vector>

#include "Common/PlatformMisc.h"
#include "Core/Logging/Log.h"
#include "Scripting/ScriptExport.h"
#include "Shader/ShaderCompiler.h"

// -------------------------------------------------------------------------
// Shader-compilation exports consumed by managed code (C# -> C++ direction).
// VspEngine.Rendering.ShaderCompiler P/Invokes these exact names from
// VspCore.dll: the managed side hands HLSL source over, the engine compiles it
// with HLSLCC and hands the SPIR-V modules and the reflection back stage by
// stage.
// Everything is plain data in/out - no exceptions cross the boundary.
// -------------------------------------------------------------------------

namespace
{
	static constexpr const char* kLogTag = "ShaderCompiler";

	// Copies UTF-8 text into a caller buffer, always terminating it. Returns the
	// number of bytes written without the terminator.
	int32 CopyTextToBuffer(const char* pTextUtf8, char* pBufferUtf8, int32 nBufferCapacityBytes)
	{
		if (pBufferUtf8 == nullptr || nBufferCapacityBytes <= 0)
		{
			return 0;
		}

		const char* pText = (pTextUtf8 != nullptr) ? pTextUtf8 : "";
		const size_t nTextByteCount = strlen(pText);
		const size_t nMaxCopyByteCount = static_cast<size_t>(nBufferCapacityBytes) - 1u;
		const size_t nCopyByteCount = nTextByteCount < nMaxCopyByteCount ? nTextByteCount : nMaxCopyByteCount;

		if (nCopyByteCount > 0)
		{
			memcpy(pBufferUtf8, pText, nCopyByteCount);
		}
		pBufferUtf8[nCopyByteCount] = '\0';
		return static_cast<int32>(nCopyByteCount);
	}

	// Validates the stage index the managed side passed.
	bool IsValidStageIndex(int32 nStage)
	{
		return nStage >= 0 && nStage < 2;
	}
}

// -------- Availability --------

CSHARP_EXPORT int32 VspShader_IsCompilerAvailable(char* pErrorUtf8, int32 nErrorCapacityBytes)
{
	Vsp::VspString sErrorText;
	const bool bIsAvailable = Vsp::ShaderCompiler::IsCompilerAvailable(sErrorText);
	if (!bIsAvailable)
	{
		CopyTextToBuffer(sErrorText.GetData(), pErrorUtf8, nErrorCapacityBytes);
	}
	return bIsAvailable ? 1 : 0;
}

// -------- Compilation --------

CSHARP_EXPORT int32 VspShader_CompileFromSource(
	const char* pSourceUtf8,
	const char* pSourceNameUtf8,
	char* pErrorUtf8,
	int32 nErrorCapacityBytes)
{
	if (pSourceUtf8 == nullptr || *pSourceUtf8 == '\0')
	{
		CopyTextToBuffer("the shader source is empty", pErrorUtf8, nErrorCapacityBytes);
		return 0;
	}

	Vsp::VspString sErrorText;
	const bool bCompiled = Vsp::ShaderCompiler::Get().CompileSource(
		Vsp::VspString(pSourceUtf8),
		(pSourceNameUtf8 != nullptr && *pSourceNameUtf8 != '\0') ? Vsp::VspString(pSourceNameUtf8) : Vsp::VspString("<memory>"),
		sErrorText);

	if (!bCompiled)
	{
		CopyTextToBuffer(sErrorText.GetData(), pErrorUtf8, nErrorCapacityBytes);
		return 0;
	}
	return 1;
}

CSHARP_EXPORT int32 VspShader_CompileFromFile(const char* pFilePathUtf8, char* pErrorUtf8, int32 nErrorCapacityBytes)
{
	if (pFilePathUtf8 == nullptr || *pFilePathUtf8 == '\0')
	{
		CopyTextToBuffer("the shader file path is empty", pErrorUtf8, nErrorCapacityBytes);
		return 0;
	}

	Vsp::VspString sErrorText;
	if (!Vsp::ShaderCompiler::Get().CompileFile(Vsp::VspString(pFilePathUtf8), sErrorText))
	{
		CopyTextToBuffer(sErrorText.GetData(), pErrorUtf8, nErrorCapacityBytes);
		return 0;
	}
	return 1;
}

// -------- Results of the last compilation --------

CSHARP_EXPORT int32 VspShader_GetCompiledStageCount()
{
	return static_cast<int32>(Vsp::ShaderCompiler::Get().GetCompiledStageCount());
}

CSHARP_EXPORT int32 VspShader_GetStageSpirvByteCount(int32 nStage)
{
	if (!IsValidStageIndex(nStage))
	{
		return 0;
	}
	return static_cast<int32>(Vsp::ShaderCompiler::Get().GetStageSpirvWords(nStage).size() * sizeof(uint32));
}

CSHARP_EXPORT int32 VspShader_CopyStageSpirv(int32 nStage, void* pBuffer, uint32 uBufferCapacity)
{
	if (!IsValidStageIndex(nStage) || pBuffer == nullptr)
	{
		return 0;
	}

	const std::vector<uint32>& spirvWords = Vsp::ShaderCompiler::Get().GetStageSpirvWords(nStage);
	const uint32 uByteCount = static_cast<uint32>(spirvWords.size() * sizeof(uint32));
	if (uByteCount == 0 || uByteCount > uBufferCapacity)
	{
		return 0;
	}

	memcpy(pBuffer, spirvWords.data(), uByteCount);
	return static_cast<int32>(uByteCount);
}

CSHARP_EXPORT int32 VspShader_GetStageEntryPointName(int32 nStage, char* pBufferUtf8, int32 nBufferCapacityBytes)
{
	if (!IsValidStageIndex(nStage))
	{
		return 0;
	}
	return CopyTextToBuffer(
		Vsp::ShaderCompiler::Get().GetStageEntryPointName(nStage), pBufferUtf8, nBufferCapacityBytes);
}

CSHARP_EXPORT int32 VspShader_GetStageReflectionSummary(int32 nStage, uint32* pOutValues)
{
	if (!IsValidStageIndex(nStage) || pOutValues == nullptr)
	{
		return 0;
	}

	const Hlslcc::ShaderStageReflection* pReflection =
		Vsp::ShaderCompiler::Get().GetStageReflection(nStage);
	if (pReflection == nullptr)
	{
		Hlslcc::ShaderStageReflection emptyReflection;
		Hlslcc::BuildReflectionSummary(emptyReflection, pOutValues);
		return 0;
	}

	Hlslcc::BuildReflectionSummary(*pReflection, pOutValues);
	return 1;
}

CSHARP_EXPORT int32 VspShader_GetReflectionJson(char* pBufferUtf8, int32 nBufferCapacityBytes)
{
	return CopyTextToBuffer(
		Vsp::ShaderCompiler::Get().GetReflectionJson().GetData(), pBufferUtf8, nBufferCapacityBytes);
}

CSHARP_EXPORT void VspShader_ClearCompiledShader()
{
	Vsp::ShaderCompiler::Get().Clear();
}

// -------- Paths the managed side needs to find shader assets --------

CSHARP_EXPORT int32 VspPlatform_GetExecutableDirectoryUtf8(char* pBufferUtf8, int32 nBufferCapacityBytes)
{
	const Vsp::VspString sExecutableDirectory = Vsp::PlatformMisc::GetExecutableDirectoryPath();
	return CopyTextToBuffer(sExecutableDirectory.GetData(), pBufferUtf8, nBufferCapacityBytes);
}