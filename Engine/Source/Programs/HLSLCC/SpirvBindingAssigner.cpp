#include "SpirvBindingAssigner.h"

#include <cstring>

#include "SpirvReflectionReader.h"

namespace Hlslcc
{
	namespace
	{
		// The engine's kind order. A resource of a kind that is not listed keeps the
		// binding the HLSL compiler gave it and is not renumbered.
		const ShaderResourceKind k_eKindOrder[] =
		{
			ShaderResourceKind::UniformBuffer,
			ShaderResourceKind::SampledImage,
			ShaderResourceKind::Sampler,
			ShaderResourceKind::StorageBuffer,
			ShaderResourceKind::StorageImage,
			ShaderResourceKind::CombinedImageSampler,
		};
		constexpr uint32_t k_nKindCount = sizeof(k_eKindOrder) / sizeof(k_eKindOrder[0]);

		// Descriptor set every engine resource lives in.
		constexpr uint32_t k_nEngineDescriptorSet = 0;

		// Where each kind starts inside that set. The numbers MUST match
		// VspCore/Graphics/ShaderBindings.h: the engine's set has ONE binding per
		// kind, and a shader that happens to declare no resource of some kind
		// must not shift the kinds that follow it. A shader that declares only a
		// sampled image therefore still reads it at the engine's sampled-image
		// binding instead of at binding 0.
		struct KindBindingBase
		{
			ShaderResourceKind eKind;
			uint32_t uBindingBase;
		};

		const KindBindingBase k_KindBindingBases[] =
		{
			{ ShaderResourceKind::UniformBuffer, 0 },   // k_nCameraUniformBuffer
			{ ShaderResourceKind::SampledImage,  1 },   // k_nBindlessTextures
			{ ShaderResourceKind::Sampler,       2 },   // k_nBindlessSampler
		};
		constexpr uint32_t k_nKindBindingBaseCount = sizeof(k_KindBindingBases) / sizeof(k_KindBindingBases[0]);

		// The binding a kind starts at, or -1 when the engine does not number
		// that kind at all (it then keeps the binding HLSL gave it).
		int32_t GetKindBindingBase(ShaderResourceKind eKind)
		{
			for (uint32_t uBaseIndex = 0; uBaseIndex < k_nKindBindingBaseCount; ++uBaseIndex)
			{
				if (k_KindBindingBases[uBaseIndex].eKind == eKind)
				{
					return static_cast<int32_t>(k_KindBindingBases[uBaseIndex].uBindingBase);
				}
			}
			return -1;
		}

		// One resource of the pass: what it is, what it is called and every place
		// the module(s) describe it.
		struct PassResource
		{
			ShaderResourceKind eKind = ShaderResourceKind::Unknown;
			char Name[k_nMaxShaderVariableNameLength] = {};
			uint32_t uBinding = 0;

			// Where the two stages describe this resource, so both can be patched.
			SpirvResourceVariable StageResources[k_nShaderStageCount];
			bool bHasStageResource[k_nShaderStageCount] = { false, false };
		};

		// Position of a kind in the engine's order, or k_nKindCount when the kind is
		// not numbered by the engine.
		uint32_t GetKindOrderIndex(ShaderResourceKind eKind)
		{
			for (uint32_t uKindIndex = 0; uKindIndex < k_nKindCount; ++uKindIndex)
			{
				if (k_eKindOrder[uKindIndex] == eKind)
				{
					return uKindIndex;
				}
			}
			return k_nKindCount;
		}

		// Finds the pass resource a stage's resource belongs to, adding it the
		// first time it is seen.
		PassResource* FindOrAddPassResource(
			std::vector<PassResource>& passResources,
			ShaderResourceKind eKind,
			const char* pName)
		{
			for (PassResource& passResource : passResources)
			{
				if (passResource.eKind == eKind && std::strcmp(passResource.Name, pName) == 0)
				{
					return &passResource;
				}
			}

			PassResource passResource;
			passResource.eKind = eKind;
			std::strncpy(passResource.Name, pName, k_nMaxShaderVariableNameLength - 1u);
			passResources.push_back(passResource);
			return &passResources.back();
		}
	}

	uint32_t SpirvBindingAssigner::GetKindCount()
	{
		return k_nKindCount;
	}

	ShaderResourceKind SpirvBindingAssigner::GetKindAt(uint32_t uKindIndex)
	{
		return (uKindIndex < k_nKindCount) ? k_eKindOrder[uKindIndex] : ShaderResourceKind::Unknown;
	}

	HlslccResult SpirvBindingAssigner::AssignPassBindings(
		std::vector<uint32_t>& vertexSpirvWords,
		std::vector<uint32_t>& fragmentSpirvWords,
		std::string& outErrorText)
	{
		outErrorText.clear();

		std::vector<uint32_t>* pStageWords[k_nShaderStageCount] = { &vertexSpirvWords, &fragmentSpirvWords };

		// ---- Read what every stage declares ----
		std::vector<PassResource> passResources;

		for (uint32_t uStageIndex = 0; uStageIndex < k_nShaderStageCount; ++uStageIndex)
		{
			if (pStageWords[uStageIndex]->size() < 5)
			{
				continue;   // The pass does not define this stage.
			}

			std::vector<SpirvResourceVariable> stageResources;
			const HlslccResult eCollectResult =
				SpirvReflectionReader::CollectResources(*pStageWords[uStageIndex], stageResources, outErrorText);
			if (eCollectResult != HlslccResult::Success)
			{
				return eCollectResult;
			}

			for (const SpirvResourceVariable& stageResource : stageResources)
			{
				PassResource* pPassResource =
					FindOrAddPassResource(passResources, stageResource.eKind, stageResource.Name);
				pPassResource->StageResources[uStageIndex] = stageResource;
				pPassResource->bHasStageResource[uStageIndex] = true;
			}
		}

		// ---- Number them by kind, in the engine's order ----
		// Every kind starts at the binding the engine reserves for it, so the
		// numbers depend on WHICH KINDS a shader declares, never on HOW MANY
		// resources of the other kinds it happens to use. Within one kind the
		// resources keep declaration order.
		for (uint32_t uKindIndex = 0; uKindIndex < k_nKindCount; ++uKindIndex)
		{
			const int32_t nBindingBase = GetKindBindingBase(k_eKindOrder[uKindIndex]);
			if (nBindingBase < 0)
			{
				continue;   // A kind the engine does not number keeps its binding.
			}

			uint32_t uKindBinding = static_cast<uint32_t>(nBindingBase);
			for (PassResource& passResource : passResources)
			{
				if (passResource.eKind != k_eKindOrder[uKindIndex])
				{
					continue;
				}

				passResource.uBinding = uKindBinding;
				++uKindBinding;
			}
		}

		// ---- Patch both stages ----
		for (uint32_t uStageIndex = 0; uStageIndex < k_nShaderStageCount; ++uStageIndex)
		{
			std::vector<uint32_t>& stageWords = *pStageWords[uStageIndex];

			for (const PassResource& passResource : passResources)
			{
				if (!passResource.bHasStageResource[uStageIndex])
				{
					continue;
				}
				if (GetKindOrderIndex(passResource.eKind) == k_nKindCount)
				{
					continue;   // A kind the engine does not number keeps its binding.
				}

				const SpirvResourceVariable& stageResource = passResource.StageResources[uStageIndex];
				if (stageResource.uBindingWordIndex == k_nNoDecorationWordIndex ||
					stageResource.uDescriptorSetWordIndex == k_nNoDecorationWordIndex)
				{
					outErrorText = std::string("the resource '") + passResource.Name +
						"' carries no descriptor set or binding decoration, so the engine's bindings "
						"cannot be assigned to it";
					return HlslccResult::FailReflection;
				}

				stageWords[stageResource.uDescriptorSetWordIndex] = k_nEngineDescriptorSet;
				stageWords[stageResource.uBindingWordIndex] = passResource.uBinding;
			}
		}

		return HlslccResult::Success;
	}
}
