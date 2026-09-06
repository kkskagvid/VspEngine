#pragma once

#include "Core/Core.h"
#include "Core/Logging/Log.h"
#include "Core/String/VspString.h"

#if VSP_PLATFORM_WINDOWS
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif // VSP_PLATFORM_WINDOWS


namespace Vsp
{
	void* LoadNativeLibrary(const VspString& path)
	{
#if VSP_PLATFORM_WINDOWS
		HMODULE lib = LoadLibraryW(path.ToWideText().GetData());
		if (!lib)
		{
			LOG_ERROR("Failed to load {}", path);
			return nullptr;
		}

		return lib;
#endif
	}

	void* GetNativeProcAddress(void* handle, const VspString& procName)
	{
#if VSP_PLATFORM_WINDOWS
		void* proc = GetProcAddress(static_cast<HMODULE>(handle), procName.GetData());
#endif
	}
}
