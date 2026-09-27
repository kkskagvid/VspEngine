// -------------------------------------------------------------------------
// Launch platform entry (Windows).
//
// The Windows half of LaunchPlatform.h: the process environment the CoreCLR
// bootstrap needs before GameEngine is initialized. It lives here - in the
// executable's own platform folder, next to wWinMain - and not in the engine
// core, because writing a process environment variable is a platform act.
// Every platform-specific call goes through Common/PlatformMisc, which is the
// engine's platform facade; the engine core never sees a Windows header.
// -------------------------------------------------------------------------

#include "Common/PlatformMisc.h"
#include "Core/Logging/Log.h"

#include "LaunchPlatform.h"

namespace Vsp
{
	static constexpr const char* kLogTag = "Launch";

	void ApplyLaunchPlatformEnvironment(const VspString& sDotNetRootPath)
	{
		if (sDotNetRootPath.IsEmpty())
		{
			// Nothing to point at: the runtime probe (and --dotnet-root) decides
			// where the runtime is, and the engine reports the failure if that
			// path cannot be loaded either.
			LOG_ERROR(kLogTag, "ApplyLaunchPlatformEnvironment: the .NET runtime root path is empty.");
			return;
		}

		// The CoreCLR host resolves nethost's get_hostfxr_path and hostpolicy from
		// DOTNET_ROOT, so the shipped runtime has to be named there before the
		// script host is initialized. Both spellings are set: a 64-bit process
		// reads DOTNET_ROOT, and the (x86) form is what a 32-bit host would look
		// at, which keeps the same launcher source usable for both.
		PlatformMisc::SetEnvironmentVariableValue("DOTNET_ROOT", sDotNetRootPath);
		PlatformMisc::SetEnvironmentVariableValue("DOTNET_ROOT(x86)", sDotNetRootPath);

		LOG_INFO(kLogTag, "DOTNET_ROOT points at the shipped .NET runtime: {}", sDotNetRootPath.GetData());
	}
}
