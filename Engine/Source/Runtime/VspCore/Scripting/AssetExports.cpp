#include "RuntimePCH.h"

#include <cstring>

#include "Classes/Material.h"
#include "Classes/Scene.h"
#include "Classes/Shader.h"
#include "Graphics/ShaderLibrary.h"
#include "Scripting/ScriptExport.h"

// -------------------------------------------------------------------------
// Shader- and material-asset exports consumed by managed code (C# -> C++).
// VspEngine.Shader and VspEngine.Material P/Invoke these exact names from
// VspCore.dll.
//
// The engine loads what HLSLCC compiled: VspShaderAsset_Load reads the shader
// manifest and the SPIR-V modules next to the executable, VspMaterial_* work on
// the native material that pairs one of those shaders with the values a
// component draws with.
// Everything is plain data in/out - no exceptions cross the boundary.
// -------------------------------------------------------------------------

namespace
{
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

	bool IsValidStageIndex(int32 nStage)
	{
		return nStage >= 0 && nStage < 2;
	}

	// Resolves the variant a caller names, falling back to the shader's default.
	const Vsp::Shader::VariantModule* ResolveVariantByIndex(const Vsp::Shader& shader, uint32 uVariantIndex)
	{
		return shader.GetVariant(uVariantIndex);
	}
}

// -------- Shaders --------

CSHARP_EXPORT uint32 VspShaderAsset_Load(const char* pShaderNameUtf8, char* pErrorUtf8, int32 nErrorCapacityBytes)
{
	if (pShaderNameUtf8 == nullptr || *pShaderNameUtf8 == '\0')
	{
		CopyTextToBuffer("the shader name is empty", pErrorUtf8, nErrorCapacityBytes);
		return Vsp::k_nInvalidObjectHandle;
	}

	Vsp::VspString sErrorText;
	const Vsp::NativeObjectHandle uShaderHandle =
		Vsp::ShaderLibrary::Get().FindOrLoadShader(Vsp::VspString(pShaderNameUtf8), sErrorText);

	if (uShaderHandle == Vsp::k_nInvalidObjectHandle)
	{
		CopyTextToBuffer(sErrorText.GetData(), pErrorUtf8, nErrorCapacityBytes);
	}
	return uShaderHandle;
}

CSHARP_EXPORT void VspShaderLibrary_SetShaderDirectory(const char* pDirectoryUtf8)
{
	Vsp::ShaderLibrary& library = Vsp::ShaderLibrary::Get();
	if (pDirectoryUtf8 == nullptr || *pDirectoryUtf8 == '\0')
	{
		library.UseDefaultShaderDirectory();
		return;
	}
	library.SetShaderDirectory(Vsp::VspString(pDirectoryUtf8));
}

CSHARP_EXPORT int32 VspShaderAsset_GetShaderName(uint32 uShaderHandle, char* pBufferUtf8, int32 nCapacityBytes)
{
	const Vsp::Shader* pShader = Vsp::Scene::Get().FindShader(uShaderHandle);
	if (pShader == nullptr)
	{
		return 0;
	}
	return CopyTextToBuffer(pShader->GetShaderName().GetData(), pBufferUtf8, nCapacityBytes);
}

CSHARP_EXPORT int32 VspShaderAsset_GetRenderQueue(uint32 uShaderHandle)
{
	const Vsp::Shader* pShader = Vsp::Scene::Get().FindShader(uShaderHandle);
	return pShader != nullptr ? static_cast<int32>(pShader->GetRenderQueue()) : 0;
}

CSHARP_EXPORT int32 VspShaderAsset_GetVariantCount(uint32 uShaderHandle)
{
	const Vsp::Shader* pShader = Vsp::Scene::Get().FindShader(uShaderHandle);
	return pShader != nullptr ? static_cast<int32>(pShader->GetVariantCount()) : 0;
}

CSHARP_EXPORT int32 VspShaderAsset_GetVariantKey(uint32 uShaderHandle, uint32 uVariantIndex, char* pBufferUtf8, int32 nCapacityBytes)
{
	const Vsp::Shader* pShader = Vsp::Scene::Get().FindShader(uShaderHandle);
	if (pShader == nullptr)
	{
		return 0;
	}

	const Vsp::Shader::VariantModule* pVariant = ResolveVariantByIndex(*pShader, uVariantIndex);
	if (pVariant == nullptr)
	{
		return 0;
	}
	return CopyTextToBuffer(pVariant->Key, pBufferUtf8, nCapacityBytes);
}

CSHARP_EXPORT int32 VspShaderAsset_GetCompiledVariantIndex(uint32 uShaderHandle, uint32 uVariantIndex)
{
	const Vsp::Shader* pShader = Vsp::Scene::Get().FindShader(uShaderHandle);
	if (pShader == nullptr)
	{
		return -1;
	}

	const Vsp::Shader::VariantModule* pVariant = ResolveVariantByIndex(*pShader, uVariantIndex);
	return pVariant != nullptr ? static_cast<int32>(pVariant->uVariantIndex) : -1;
}

CSHARP_EXPORT int32 VspShaderAsset_GetEntryPointName(
	uint32 uShaderHandle,
	uint32 uVariantIndex,
	int32 nStage,
	char* pBufferUtf8,
	int32 nCapacityBytes)
{
	if (!IsValidStageIndex(nStage))
	{
		return 0;
	}

	const Vsp::Shader* pShader = Vsp::Scene::Get().FindShader(uShaderHandle);
	if (pShader == nullptr)
	{
		return 0;
	}

	const Vsp::Shader::VariantModule* pVariant = ResolveVariantByIndex(*pShader, uVariantIndex);
	if (pVariant == nullptr || pVariant->uPassCount == 0)
	{
		return 0;
	}
	return CopyTextToBuffer(pVariant->Passes[0].Stages[nStage].EntryPointName, pBufferUtf8, nCapacityBytes);
}

CSHARP_EXPORT int32 VspShaderAsset_GetStageSpirvByteCount(uint32 uShaderHandle, uint32 uVariantIndex, int32 nStage)
{
	if (!IsValidStageIndex(nStage))
	{
		return 0;
	}

	const Vsp::Shader* pShader = Vsp::Scene::Get().FindShader(uShaderHandle);
	if (pShader == nullptr)
	{
		return 0;
	}

	const Vsp::Shader::VariantModule* pVariant = ResolveVariantByIndex(*pShader, uVariantIndex);
	if (pVariant == nullptr || pVariant->uPassCount == 0)
	{
		return 0;
	}
	return static_cast<int32>(pVariant->Passes[0].Stages[nStage].GetSpirvByteCount());
}

CSHARP_EXPORT int32 VspShaderAsset_CopyStageSpirv(
	uint32 uShaderHandle,
	uint32 uVariantIndex,
	int32 nStage,
	void* pBuffer,
	uint32 uBufferCapacity)
{
	if (!IsValidStageIndex(nStage) || pBuffer == nullptr)
	{
		return 0;
	}

	const Vsp::Shader* pShader = Vsp::Scene::Get().FindShader(uShaderHandle);
	if (pShader == nullptr)
	{
		return 0;
	}

	const Vsp::Shader::VariantModule* pVariant = ResolveVariantByIndex(*pShader, uVariantIndex);
	if (pVariant == nullptr || pVariant->uPassCount == 0)
	{
		return 0;
	}

	const Vsp::Shader::StageModule& stage = pVariant->Passes[0].Stages[nStage];
	const uint32 uByteCount = stage.GetSpirvByteCount();
	if (uByteCount == 0 || uByteCount > uBufferCapacity)
	{
		return 0;
	}

	memcpy(pBuffer, stage.SpirvWords.data(), uByteCount);
	return static_cast<int32>(uByteCount);
}

CSHARP_EXPORT int32 VspShaderAsset_GetStageReflectionSummary(
	uint32 uShaderHandle,
	uint32 uVariantIndex,
	int32 nStage,
	uint32* pOutValues)
{
	if (!IsValidStageIndex(nStage) || pOutValues == nullptr)
	{
		return 0;
	}

	// Same layout as VspShader_GetStageReflectionSummary: input count, output
	// count, resource count, push-constant member count, push-constant size.
	for (uint32 uValueIndex = 0; uValueIndex < 5; ++uValueIndex)
	{
		pOutValues[uValueIndex] = 0;
	}

	const Vsp::Shader* pShader = Vsp::Scene::Get().FindShader(uShaderHandle);
	if (pShader == nullptr)
	{
		return 0;
	}

	const Vsp::Shader::VariantModule* pVariant = ResolveVariantByIndex(*pShader, uVariantIndex);
	if (pVariant == nullptr || pVariant->uPassCount == 0)
	{
		return 0;
	}

	const Vsp::Shader::StageModule& stage = pVariant->Passes[0].Stages[nStage];
	pOutValues[0] = stage.uInputCount;
	pOutValues[1] = stage.uOutputCount;
	pOutValues[2] = stage.uResourceCount;
	pOutValues[3] = 0;   // The manifest carries the size, not the member count.
	pOutValues[4] = stage.uPushConstantByteSize;
	return 1;
}

CSHARP_EXPORT int32 VspShaderAsset_GetPropertyCount(uint32 uShaderHandle)
{
	const Vsp::Shader* pShader = Vsp::Scene::Get().FindShader(uShaderHandle);
	return pShader != nullptr ? static_cast<int32>(pShader->GetPropertyCount()) : 0;
}

CSHARP_EXPORT int32 VspShaderAsset_GetProperty(
	uint32 uShaderHandle,
	uint32 uPropertyIndex,
	char* pNameUtf8,
	int32 nNameCapacityBytes,
	char* pDisplayNameUtf8,
	int32 nDisplayNameCapacityBytes,
	int32* pOutType,
	float* pOutDefaults4)
{
	const Vsp::Shader* pShader = Vsp::Scene::Get().FindShader(uShaderHandle);
	if (pShader == nullptr)
	{
		return 0;
	}

	const Vsp::Shader::Property* pProperty = pShader->GetProperty(uPropertyIndex);
	if (pProperty == nullptr)
	{
		return 0;
	}

	CopyTextToBuffer(pProperty->Name, pNameUtf8, nNameCapacityBytes);
	CopyTextToBuffer(pProperty->DisplayName, pDisplayNameUtf8, nDisplayNameCapacityBytes);

	if (pOutType != nullptr)
	{
		*pOutType = static_cast<int32>(pProperty->eType);
	}
	if (pOutDefaults4 != nullptr)
	{
		memcpy(pOutDefaults4, pProperty->fDefaultValues, sizeof(pProperty->fDefaultValues));
	}
	return 1;
}

CSHARP_EXPORT int32 VspShaderAsset_GetKeywordGroupCount(uint32 uShaderHandle)
{
	const Vsp::Shader* pShader = Vsp::Scene::Get().FindShader(uShaderHandle);
	return pShader != nullptr ? static_cast<int32>(pShader->GetKeywordGroupCount()) : 0;
}

CSHARP_EXPORT int32 VspShaderAsset_GetKeywordGroup(
	uint32 uShaderHandle,
	uint32 uGroupIndex,
	char* pNameUtf8,
	int32 nNameCapacityBytes,
	int32* pOutKind,
	int32* pOutStrippable)
{
	const Vsp::Shader* pShader = Vsp::Scene::Get().FindShader(uShaderHandle);
	if (pShader == nullptr)
	{
		return 0;
	}

	const Vsp::Shader::KeywordGroup* pGroup = pShader->GetKeywordGroup(uGroupIndex);
	if (pGroup == nullptr)
	{
		return 0;
	}

	CopyTextToBuffer(pGroup->Name, pNameUtf8, nNameCapacityBytes);
	if (pOutKind != nullptr)
	{
		*pOutKind = static_cast<int32>(pGroup->eKind);
	}
	if (pOutStrippable != nullptr)
	{
		*pOutStrippable = pGroup->IsStrippable() ? 1 : 0;
	}
	return static_cast<int32>(pGroup->uKeywordStateCount);
}

CSHARP_EXPORT int32 VspShaderAsset_GetKeywordGroupState(
	uint32 uShaderHandle,
	uint32 uGroupIndex,
	uint32 uStateIndex,
	char* pBufferUtf8,
	int32 nCapacityBytes)
{
	const Vsp::Shader* pShader = Vsp::Scene::Get().FindShader(uShaderHandle);
	if (pShader == nullptr)
	{
		return 0;
	}

	const Vsp::Shader::KeywordGroup* pGroup = pShader->GetKeywordGroup(uGroupIndex);
	if (pGroup == nullptr || uStateIndex >= pGroup->uKeywordStateCount)
	{
		return 0;
	}
	return CopyTextToBuffer(pGroup->KeywordStates[uStateIndex], pBufferUtf8, nCapacityBytes);
}

// -------- Materials --------

CSHARP_EXPORT uint32 VspMaterial_Create(uint32 uShaderHandle, char* pErrorUtf8, int32 nErrorCapacityBytes)
{
	Vsp::VspString sErrorText;
	const Vsp::NativeObjectHandle uMaterialHandle =
		Vsp::ShaderLibrary::Get().CreateMaterial(uShaderHandle, sErrorText);

	if (uMaterialHandle == Vsp::k_nInvalidObjectHandle)
	{
		CopyTextToBuffer(sErrorText.GetData(), pErrorUtf8, nErrorCapacityBytes);
	}
	return uMaterialHandle;
}

CSHARP_EXPORT int32 VspMaterial_Destroy(uint32 uMaterialHandle)
{
	return Vsp::Scene::Get().DestroyMaterial(uMaterialHandle) ? 1 : 0;
}

CSHARP_EXPORT uint32 VspMaterial_GetShader(uint32 uMaterialHandle)
{
	const Vsp::Material* pMaterial = Vsp::Scene::Get().FindMaterial(uMaterialHandle);
	return pMaterial != nullptr ? pMaterial->GetShaderHandle() : Vsp::k_nInvalidObjectHandle;
}

CSHARP_EXPORT int32 VspMaterial_SetShader(uint32 uMaterialHandle, uint32 uShaderHandle)
{
	Vsp::Material* pMaterial = Vsp::Scene::Get().FindMaterial(uMaterialHandle);
	const Vsp::Shader* pShader = Vsp::Scene::Get().FindShader(uShaderHandle);
	if (pMaterial == nullptr || pShader == nullptr)
	{
		return 0;
	}

	pMaterial->SetShaderHandle(uShaderHandle);
	pMaterial->InitializeFromShader(*pShader);
	return 1;
}

CSHARP_EXPORT int32 VspMaterial_GetPropertyCount(uint32 uMaterialHandle)
{
	const Vsp::Material* pMaterial = Vsp::Scene::Get().FindMaterial(uMaterialHandle);
	return pMaterial != nullptr ? static_cast<int32>(pMaterial->GetPropertyValueCount()) : 0;
}

CSHARP_EXPORT int32 VspMaterial_GetPropertyName(
	uint32 uMaterialHandle,
	uint32 uValueIndex,
	char* pBufferUtf8,
	int32 nCapacityBytes)
{
	const Vsp::Material* pMaterial = Vsp::Scene::Get().FindMaterial(uMaterialHandle);
	if (pMaterial == nullptr)
	{
		return 0;
	}

	const Vsp::Material::PropertyValue* pValue = pMaterial->GetPropertyValue(uValueIndex);
	if (pValue == nullptr)
	{
		return 0;
	}
	return CopyTextToBuffer(pValue->Name, pBufferUtf8, nCapacityBytes);
}

CSHARP_EXPORT int32 VspMaterial_SetFloat(uint32 uMaterialHandle, const char* pPropertyNameUtf8, float fValue)
{
	Vsp::Material* pMaterial = Vsp::Scene::Get().FindMaterial(uMaterialHandle);
	return (pMaterial != nullptr && pMaterial->SetFloat(pPropertyNameUtf8, fValue)) ? 1 : 0;
}

CSHARP_EXPORT int32 VspMaterial_GetFloat(uint32 uMaterialHandle, const char* pPropertyNameUtf8, float* pOutValue)
{
	const Vsp::Material* pMaterial = Vsp::Scene::Get().FindMaterial(uMaterialHandle);
	if (pMaterial == nullptr || pOutValue == nullptr)
	{
		return 0;
	}
	return pMaterial->GetFloat(pPropertyNameUtf8, *pOutValue) ? 1 : 0;
}

CSHARP_EXPORT int32 VspMaterial_SetVector(
	uint32 uMaterialHandle,
	const char* pPropertyNameUtf8,
	float fValueX,
	float fValueY,
	float fValueZ,
	float fValueW)
{
	Vsp::Material* pMaterial = Vsp::Scene::Get().FindMaterial(uMaterialHandle);
	if (pMaterial == nullptr)
	{
		return 0;
	}

	const float fValues[4] = { fValueX, fValueY, fValueZ, fValueW };
	return pMaterial->SetVector(pPropertyNameUtf8, fValues) ? 1 : 0;
}

CSHARP_EXPORT int32 VspMaterial_GetVector(uint32 uMaterialHandle, const char* pPropertyNameUtf8, float* pOutValues4)
{
	const Vsp::Material* pMaterial = Vsp::Scene::Get().FindMaterial(uMaterialHandle);
	if (pMaterial == nullptr || pOutValues4 == nullptr)
	{
		return 0;
	}
	return pMaterial->GetVector(pPropertyNameUtf8, pOutValues4) ? 1 : 0;
}

CSHARP_EXPORT int32 VspMaterial_SetKeywordEnabled(uint32 uMaterialHandle, const char* pKeywordUtf8, int32 bIsEnabled)
{
	Vsp::Material* pMaterial = Vsp::Scene::Get().FindMaterial(uMaterialHandle);
	return (pMaterial != nullptr && pMaterial->SetKeywordEnabled(pKeywordUtf8, bIsEnabled != 0)) ? 1 : 0;
}

CSHARP_EXPORT int32 VspMaterial_IsKeywordEnabled(uint32 uMaterialHandle, const char* pKeywordUtf8)
{
	const Vsp::Material* pMaterial = Vsp::Scene::Get().FindMaterial(uMaterialHandle);
	return (pMaterial != nullptr && pMaterial->IsKeywordEnabled(pKeywordUtf8)) ? 1 : 0;
}

CSHARP_EXPORT int32 VspMaterial_ResolveVariantIndex(uint32 uMaterialHandle)
{
	const Vsp::Material* pMaterial = Vsp::Scene::Get().FindMaterial(uMaterialHandle);
	if (pMaterial == nullptr)
	{
		return -1;
	}

	const Vsp::Shader* pShader = Vsp::Scene::Get().FindShader(pMaterial->GetShaderHandle());
	if (pShader == nullptr)
	{
		return -1;
	}

	const Vsp::Shader::VariantModule* pVariant = pMaterial->ResolveVariant(*pShader);
	return pVariant != nullptr ? static_cast<int32>(pVariant->uVariantIndex) : -1;
}

// -------- Component material --------

CSHARP_EXPORT uint32 VspComponent_GetMaterial(uint32 uComponentHandle)
{
	const Vsp::Component* pComponent = Vsp::Scene::Get().FindComponent(uComponentHandle);
	return pComponent != nullptr ? pComponent->GetRenderState().uMaterialHandle : Vsp::k_nInvalidObjectHandle;
}

CSHARP_EXPORT void VspComponent_SetMaterial(uint32 uComponentHandle, uint32 uMaterialHandle)
{
	Vsp::Component* pComponent = Vsp::Scene::Get().FindComponent(uComponentHandle);
	if (pComponent != nullptr)
	{
		pComponent->GetMutableRenderState().uMaterialHandle = uMaterialHandle;
	}
}
