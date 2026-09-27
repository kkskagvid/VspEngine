#pragma once

#include "Core/Core.h"

namespace Vsp
{
	// -------------------------------------------------------------------------
	// EngineServiceEntry
	// -------------------------------------------------------------------------
	// One live engine service as the registry owns it: its name (for the log), its
	// storage, how to create and destroy it, and which thread created it.
	//
	// It is a plain aggregate on purpose: the registry is the only thing that
	// writes it, and a service lookup must stay cheap enough to sit on a managed
	// interop path.
	// -------------------------------------------------------------------------
	struct EngineServiceEntry
	{
		// Human-readable service name ("Scene", "InputManager", ...).
		const char* pServiceName = nullptr;

		// The service object, owned by the registry.
		void* pService = nullptr;

		// Creates a TService on the heap; the engine's services have private
		// constructors, so only EngineServices (a friend) can call this.
		void* (*pCreateService)() = nullptr;

		// Destroys what pCreateService created.
		void (*pDestroyService)(void*) = nullptr;

		// Thread that created the service; it is the only thread allowed to use it.
		uint64 uOwnerThreadId = 0;

		// True once ShutdownAll() destroyed the service.
		bool bIsDestroyed = false;

		// True once a call from another thread was reported, so it is reported once.
		bool bHasReportedForeignThreadAccess = false;
	};

	// -------------------------------------------------------------------------
	// EngineServices
	// -------------------------------------------------------------------------
	// The engine's process-wide services - the scene, the clock, the input state,
	// the graphics front end, the shader library, the physics world, the text
	// service, the localisation service, the output devices and the script host -
	// are SINGLETONS: there is one window, one scene and one clock in a process.
	// This is the one place that owns their storage, their creation, their
	// DESTRUCTION and the rule about which thread may use them.
	//
	// Why not one function-local static per service (what the engine used to do):
	//
	//   * CREATION is thread safe either way - a magic static's initializer runs
	//     exactly once, even when two threads arrive together (C++20 [stmt.dcl]/4,
	//     and MSVC's /Zc:threadSafeInit, which is on by default) - but a bare
	//     magic static cannot say WHO created the service, so a use from another
	//     thread cannot be reported, only raced;
	//   * DESTRUCTION has no order the compiler can know about: magic statics are
	//     destroyed at process exit in reverse order of construction, interleaved
	//     with every other static in the process. A service could therefore be
	//     destroyed before (or after) something it points at - a log backend, an
	//     output device, a window - and nothing would be able to report it. Here
	//     ShutdownAll() destroys them EXPLICITLY, in reverse creation order, at a
	//     point the host chooses;
	//   * a service used AFTER that point must not be a dangling pointer: it is
	//     reported and re-created instead.
	//
	// The thread rule: a service belongs to the thread that created it (the
	// engine's main thread - the one that runs frames). A lookup from any other
	// thread is reported once per service through the log, because the services
	// themselves are not synchronized: the engine runs its frame on one thread,
	// and a service touched from another is a bug in the caller, not something
	// this table can fix by hiding it behind a lock.
	//
	// Nothing here throws, and no platform header is included: the current thread
	// is named through Common/PlatformMisc, the engine's platform facade.
	// -------------------------------------------------------------------------
	class RUNTIME_API EngineServices
	{
	public:
		// The service of the given type, created on first use. TService must be
		// default-constructible and must declare EngineServices as a friend
		// (its constructor is private, like every other engine service).
		//
		// pServiceName is only used for diagnostics; the same type always resolves
		// to the same service.
		template <typename TService>
		static TService& GetService(const char* pServiceName)
		{
			// The slot is what makes the FIRST lookup safe: its initializer runs
			// exactly once, under the registry's own lock, and creates the service.
			static EngineServiceEntry* s_pEntry =
				EnsureServiceEntry(pServiceName, &CreateService<TService>, &DestroyService<TService>);

			if (s_pEntry->bIsDestroyed)
			{
				// The engine was shut down and something is still asking for a
				// service: report it and start a fresh one rather than hand out a
				// destroyed object.
				s_pEntry = ReviveServiceEntry(*s_pEntry);
			}

			ReportServiceAccess(*s_pEntry);
			return *static_cast<TService*>(s_pEntry->pService);
		}

		// Destroys every live service, most recently created first, so a service is
		// gone before anything it was built on. The host calls it once, after the
		// last log line and after the backends that point at the output devices
		// have been removed.
		static void ShutdownAll();

		// Number of services that are currently alive (diagnostics / tests).
		static uint32 GetLiveServiceCount();

		// True once ShutdownAll() has run (diagnostics / tests).
		static bool HasShutDown();

	private:
		// Finds the entry of a service that was already created, or creates the
		// service and its entry. Thread safe (the registry's lock serializes it).
		static EngineServiceEntry* EnsureServiceEntry(
			const char* pServiceName,
			void* (*pCreateService)(),
			void (*pDestroyService)(void*));

		// Creates the service of a destroyed entry again and reports it.
		static EngineServiceEntry* ReviveServiceEntry(EngineServiceEntry& Entry);

		// Reports a lookup from a thread that does not own the service (once).
		static void ReportServiceAccess(EngineServiceEntry& Entry);

		template <typename TService>
		static void* CreateService()
		{
			return new TService();
		}

		template <typename TService>
		static void DestroyService(void* pService)
		{
			delete static_cast<TService*>(pService);
		}
	};
}
