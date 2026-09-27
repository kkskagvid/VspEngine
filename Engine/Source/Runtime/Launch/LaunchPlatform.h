#pragma once

#include "Core/String/VspString.h"

namespace Vsp
{
	// -------------------------------------------------------------------------
	// LaunchPlatform
	// -------------------------------------------------------------------------
	// The process environment a platform has to have in place BEFORE the engine
	// starts: everything the host's CoreCLR bootstrap resolves from the
	// environment rather than from a parameter - DOTNET_ROOT above all, which is
	// what nethost's get_hostfxr_path and hostpolicy look the shipped runtime up
	// through.
	//
	// It belongs to the HOST, not to the engine: setting a process-wide
	// environment variable is a platform act, and the engine core must run on a
	// platform whose runtime is resolved differently (or not at all). The
	// declaration is platform independent; each platform implements it in its own
	// translation unit under Launch/<Platform>/, next to that platform's entry
	// point (Launch/Windows/LaunchPlatformWindows.cpp for Windows).
	//
	// The implementation logs what it does through the engine Log module, logs
	// its own errors and never throws.
	// -------------------------------------------------------------------------
	void ApplyLaunchPlatformEnvironment(const VspString& sDotNetRootPath);
}
