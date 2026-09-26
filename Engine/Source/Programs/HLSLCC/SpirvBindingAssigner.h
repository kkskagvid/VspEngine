#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Result.h"
#include "ShaderReflection.h"

namespace Hlslcc
{
	// -------------------------------------------------------------------------
	// SpirvBindingAssigner
	// -------------------------------------------------------------------------
	// Assigns the descriptor bindings of a compiled shader. A shader never writes
	// [[vk::binding]] itself: the ENGINE decides where its resources live, and
	// this is where that decision is applied to the compiled SPIR-V.
	//
	// The engine's set is descriptor set 0, and each RESOURCE KIND has its own
	// binding there (VspCore/Graphics/ShaderBindings.h):
	//
	//   uniform buffers -> 0    sampled images -> 1    samplers -> 2
	//
	// so the engine's camera block lands on binding 0, the bindless texture array
	// on binding 1 and the shared sampler on binding 2, whatever a shader calls
	// them and in whatever order it declares them. A shader that declares no
	// resource of some kind does NOT shift the kinds after it: a shader that only
	// samples textures still reads them at binding 1. Kinds the engine does not
	// number (storage buffers, storage images, combined image samplers) keep the
	// binding the HLSL compiler gave them.
	//
	// More than one resource of one numbered kind is numbered on from that kind's
	// binding, so a shader that declares two sampled images puts them at 1 and 2.
	// The engine only ever provides one of each, which is what the runtime check
	// on load is for.
	//
	// The numbering runs over the WHOLE PASS, not over one stage: the vertex and
	// the fragment stage of a pass share one descriptor set, so a resource both
	// stages use gets one binding. Resources are matched by kind and name, which
	// is exactly how the two stages refer to the same resource.
	//
	// Nothing throws: a module that cannot be read is reported through a
	// HlslccResult and a message.
	// -------------------------------------------------------------------------
	class SpirvBindingAssigner
	{
	public:
		// Rewrites the descriptor set and binding of every resource of a pass in
		// place. Either module may be empty (a pass that only defines one stage).
		// Returns FailReflection when a module cannot be read, or when a resource
		// carries no binding decoration at all (which no compiler produces, so it
		// means the module is not a shader module).
		static HlslccResult AssignPassBindings(
			std::vector<uint32_t>& vertexSpirvWords,
			std::vector<uint32_t>& fragmentSpirvWords,
			std::string& outErrorText);

		// Number of kind groups the engine numbers in; exposed so the rule can be
		// reported and tested.
		static uint32_t GetKindCount();

		// The kind numbered at the given position of the engine's order.
		static ShaderResourceKind GetKindAt(uint32_t uKindIndex);
	};
}
