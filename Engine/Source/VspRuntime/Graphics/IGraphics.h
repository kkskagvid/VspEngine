#pragma once

#include "Core/Core.h"
#include "Core/String/VspString.h"

namespace Vsp
{
	// -------------------------------------------------------------------------
	// IGraphics
	// -------------------------------------------------------------------------
	// Minimal interface shared by graphics backends. Implementations never
	// throw: failures are reported through outErrorText.
	// -------------------------------------------------------------------------
	class RUNTIME_API IGraphics
	{
	public:
		virtual ~IGraphics() = default;

		virtual bool Initialize(void* pNativeWindowHandle, VspString& outErrorText) = 0;
		virtual void Shutdown() = 0;
		virtual void OnWindowResize(uint32_t uWidth, uint32_t uHeight) = 0;
		virtual bool RenderFrame(VspString& outErrorText) = 0;
	};
}
