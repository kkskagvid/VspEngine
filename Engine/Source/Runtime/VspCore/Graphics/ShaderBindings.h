#pragma once

#include "Core/Core.h"

namespace Vsp
{
	// -------------------------------------------------------------------------
	// ShaderBindings
	// -------------------------------------------------------------------------
	// Where the resources a shader uses live. THE ENGINE DECIDES THIS, not the
	// shader: a .vsf shader declares which resources it reads (a camera block, the
	// bindless texture array, the shared sampler) and never writes [[vk::binding]].
	// HLSLCC applies these numbers to the compiled SPIR-V (see its
	// SpirvBindingAssigner) and reports them in the shader manifest, so the engine
	// can check that a shader it loads really uses the set it provides.
	//
	// The numbering is by RESOURCE KIND: the engine's camera block always lands on
	// the uniform-buffer binding, the bindless array on the sampled-image binding
	// and the shared sampler on the sampler binding, whatever a shader calls them.
	// -------------------------------------------------------------------------
	struct RUNTIME_API ShaderBindings
	{
		// Descriptor set every engine resource lives in. The bindings are
		// declared for BOTH programmable stages, so a shader may read any of them
		// from whichever stage it needs (a vertex stage transforming by the camera
		// and a fragment stage taking the camera position from it are both fine).
		static constexpr uint32 k_nDescriptorSet = 0;

		// Binding of the per-frame camera block (a uniform buffer).
		static constexpr uint32 k_nCameraUniformBuffer = 0;

		// Binding of the bindless sampled-image array.
		static constexpr uint32 k_nBindlessTextures = 1;

		// Binding of the sampler those images are read with.
		static constexpr uint32 k_nBindlessSampler = 2;

		// Number of bindings the engine's descriptor set layout declares.
		static constexpr uint32 k_nBindingCount = 3;
	};
}
