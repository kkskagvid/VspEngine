#pragma once

#include "Common/PlatformMisc.h"

namespace Vsp
{
	// -------------------------------------------------------------------------
	// NativeApi
	// -------------------------------------------------------------------------
	// Convenience wrappers for loading a native dynamic library and resolving
	// its exported functions. All platform-specific work (the actual Windows
	// calls) is encapsulated in Common/PlatformMisc behind
	// #if VSP_PLATFORM_WINDOWS - nothing here includes a platform header.
	// -------------------------------------------------------------------------

	// Loads the dynamic library at the given path; nullptr on failure (the
	// error is already logged).
	inline void* LoadNativeLibrary(const VspString& sFilePath)
	{
		return PlatformMisc::LoadDynamicLibrary(sFilePath);
	}

	// Resolves an exported function by name; nullptr when the export is
	// missing.
	inline void* GetNativeProcAddress(void* pLibraryHandle, const VspString& sFunctionName)
	{
		return PlatformMisc::GetDynamicLibraryFunction(pLibraryHandle, sFunctionName.GetData());
	}
}
