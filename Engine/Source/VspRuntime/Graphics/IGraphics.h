#pragma once

#include "Core/Core.h"

namespace Vsp
{
	// -------------------------------------------------------------------------
	// IGraphics
	// -------------------------------------------------------------------------
	// Minimal interface shared by graphics backends. Implementations never
	// throw and report failures through the engine log (Log) instead of
	// out-parameters: every function logs its own errors internally.
	// -------------------------------------------------------------------------
	class RUNTIME_API IGraphics
	{
	public:
		virtual ~IGraphics() = default;

		virtual bool Initialize(void* pNativeWindowHandle) = 0;
		virtual void Shutdown() = 0;
		virtual void OnWindowResize(uint32 uWidth, uint32 uHeight) = 0;
		virtual bool RenderFrame() = 0;
	};
}
