#include "ShaderReflection.h"

namespace Hlslcc
{
	const char* ToProfileName(ShaderStage eStage)
	{
		switch (eStage)
		{
		case ShaderStage::Vertex:   return "vs_6_0";
		case ShaderStage::Fragment: return "ps_6_0";
		default:                    return "vs_6_0";
		}
	}

	const char* ToStageName(ShaderStage eStage)
	{
		switch (eStage)
		{
		case ShaderStage::Vertex:   return "vertex";
		case ShaderStage::Fragment: return "fragment";
		default:                    return "unknown";
		}
	}

	const char* ToStageFileExtension(ShaderStage eStage)
	{
		switch (eStage)
		{
		case ShaderStage::Vertex:   return "vert";
		case ShaderStage::Fragment: return "frag";
		default:                    return "spv";
		}
	}

	const char* ToResourceKindName(ShaderResourceKind eKind)
	{
		switch (eKind)
		{
		case ShaderResourceKind::UniformBuffer:       return "uniformBuffer";
		case ShaderResourceKind::StorageBuffer:       return "storageBuffer";
		case ShaderResourceKind::SampledImage:        return "sampledImage";
		case ShaderResourceKind::StorageImage:        return "storageImage";
		case ShaderResourceKind::Sampler:             return "sampler";
		case ShaderResourceKind::CombinedImageSampler: return "combinedImageSampler";
		default:                                      return "unknown";
		}
	}

	void BuildReflectionSummary(const ShaderStageReflection& reflection, uint32_t* pOutValues)
	{
		if (pOutValues == nullptr)
		{
			return;
		}

		pOutValues[0] = reflection.uInputCount;
		pOutValues[1] = reflection.uOutputCount;
		pOutValues[2] = reflection.uResourceCount;
		pOutValues[3] = reflection.uPushConstantMemberCount;
		pOutValues[4] = reflection.uPushConstantByteSize;
		pOutValues[5] = 0;
	}
}
