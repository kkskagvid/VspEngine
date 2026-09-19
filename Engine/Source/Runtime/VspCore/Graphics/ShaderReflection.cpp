#include "RuntimePCH.h"

#include <cstring>

#include "Graphics/ShaderReflection.h"

namespace Vsp
{
	const char* ToShaderResourceKindName(ShaderResourceKind eKind)
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

	ShaderResourceKind ParseShaderResourceKindName(const char* pKindName)
	{
		if (pKindName == nullptr)
		{
			return ShaderResourceKind::Unknown;
		}

		// The names come from the manifest, which HLSLCC writes.
		struct KindName
		{
			const char* pName;
			ShaderResourceKind eKind;
		};
		static const KindName s_kindNames[] =
		{
			{ "uniformBuffer",        ShaderResourceKind::UniformBuffer },
			{ "storageBuffer",        ShaderResourceKind::StorageBuffer },
			{ "sampledImage",         ShaderResourceKind::SampledImage },
			{ "storageImage",         ShaderResourceKind::StorageImage },
			{ "sampler",              ShaderResourceKind::Sampler },
			{ "combinedImageSampler", ShaderResourceKind::CombinedImageSampler },
		};

		for (const KindName& kindName : s_kindNames)
		{
			if (std::strcmp(kindName.pName, pKindName) == 0)
			{
				return kindName.eKind;
			}
		}
		return ShaderResourceKind::Unknown;
	}
}
