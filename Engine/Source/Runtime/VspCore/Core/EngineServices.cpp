#include "RuntimePCH.h"

#include <mutex>

#include "Common/PlatformMisc.h"
#include "Core/EngineServices.h"
#include "Core/Logging/Log.h"
#include "Core/Templates/ArrayList.h"

namespace Vsp
{
	static constexpr const char* kLogTag = "EngineServices";

	namespace
	{
		// ---------------------------------------------------------------------
		// The registry table
		// ---------------------------------------------------------------------
		// One table for the whole process, plus the lock that serializes creating
		// a service, shutting every service down and reviving one. The lock is
		// only ever taken on those three paths: a plain lookup reads the entry the
		// calling type already resolved.
		// ---------------------------------------------------------------------
		struct ServiceTable
		{
			std::mutex Mutex;
			ArrayList<EngineServiceEntry*> Entries;
			bool bHasShutDown = false;
		};

		ServiceTable& GetServiceTable()
		{
			// The table itself is a magic static: its construction is thread safe,
			// and it holds no service of its own - only pointers the registry owns.
			static ServiceTable s_Table;
			return s_Table;
		}

		// Creates the service behind an entry (the entry's own creator).
		bool CreateServiceOfEntry(EngineServiceEntry& Entry)
		{
			if (Entry.pCreateService == nullptr)
			{
				LOG_ERROR(kLogTag, "The engine service '{}' has no creator.", Entry.pServiceName);
				return false;
			}

			Entry.pService = Entry.pCreateService();
			if (Entry.pService == nullptr)
			{
				LOG_ERROR(kLogTag, "The engine service '{}' could not be created.", Entry.pServiceName);
				return false;
			}
			return true;
		}
	}

	// -------------------------------------------------------------------------
	// Lookup
	// -------------------------------------------------------------------------

	EngineServiceEntry* EngineServices::EnsureServiceEntry(
		const char* pServiceName,
		void* (*pCreateService)(),
		void (*pDestroyService)(void*))
	{
		ServiceTable& table = GetServiceTable();
		std::lock_guard<std::mutex> lock(table.Mutex);

		// A service is created once: the slot of the calling type points at the
		// entry for as long as the process lives, so a second lookup only has to
		// read it. Entries are never removed, so a slot can never dangle; a
		// destroyed entry is revived in place.
		EngineServiceEntry* pEntry = new EngineServiceEntry();
		pEntry->pServiceName = (pServiceName != nullptr) ? pServiceName : "EngineService";
		pEntry->pCreateService = pCreateService;
		pEntry->pDestroyService = pDestroyService;
		pEntry->uOwnerThreadId = PlatformMisc::GetCurrentThreadId();

		if (!CreateServiceOfEntry(*pEntry))
		{
			// The entry stays in the table without a service: a caller that gets
			// here twice is reported by the create path again, and nothing dangles.
			pEntry->bIsDestroyed = true;
		}

		table.Entries.Add(pEntry);
		return pEntry;
	}

	EngineServiceEntry* EngineServices::ReviveServiceEntry(EngineServiceEntry& Entry)
	{
		ServiceTable& table = GetServiceTable();
		std::lock_guard<std::mutex> lock(table.Mutex);

		LOG_ERROR(kLogTag,
			"The engine service '{}' was used after EngineServices::ShutdownAll(); it is created again.",
			Entry.pServiceName);

		Entry.bIsDestroyed = false;
		if (!CreateServiceOfEntry(Entry))
		{
			Entry.bIsDestroyed = true;
		}
		return &Entry;
	}

	void EngineServices::ReportServiceAccess(EngineServiceEntry& Entry)
	{
		if (Entry.bHasReportedForeignThreadAccess)
		{
			return;
		}

		// A service belongs to the thread that created it: the engine runs its
		// frame - messages, scripts, physics and the recorded frame - on one
		// thread, and the services themselves are not synchronized. Reporting the
		// first foreign access is what turns "it usually works" into a fact.
		const uint64 uCurrentThreadId = PlatformMisc::GetCurrentThreadId();
		if (uCurrentThreadId == 0 || uCurrentThreadId == Entry.uOwnerThreadId)
		{
			return;
		}

		Entry.bHasReportedForeignThreadAccess = true;
		LOG_ERROR(kLogTag,
			"The engine service '{}' belongs to thread {} but was used from thread {}; engine services are "
			"main-thread only (the frame loop owns them).",
			Entry.pServiceName, Entry.uOwnerThreadId, uCurrentThreadId);
	}

	// -------------------------------------------------------------------------
	// Shutdown
	// -------------------------------------------------------------------------

	void EngineServices::ShutdownAll()
	{
		ServiceTable& table = GetServiceTable();
		std::lock_guard<std::mutex> lock(table.Mutex);

		if (table.bHasShutDown)
		{
			LOG_WARNING(kLogTag, "ShutdownAll was called twice; the second call does nothing.");
			return;
		}
		table.bHasShutDown = true;

		// The count is reported BEFORE anything is destroyed: one of the services
		// is the output-device registry the log backends write through, so the
		// last line of the engine has to be written while every service is intact.
		uint32 uLiveCount = 0;
		for (size_t nEntryIndex = 0; nEntryIndex < table.Entries.GetSize(); ++nEntryIndex)
		{
			const EngineServiceEntry* pEntry = table.Entries[nEntryIndex];
			if (pEntry != nullptr && !pEntry->bIsDestroyed && pEntry->pService != nullptr)
			{
				++uLiveCount;
			}
		}
		LOG_INFO(kLogTag, "Shutting the engine services down: {} service(s), most recently created first.", uLiveCount);

		// Reverse creation order: a service is destroyed before the services it was
		// built on (the scene before the localisation service it logs through, the
		// input manager before the window it drives).
		uint32 uDestroyedCount = 0;
		for (size_t nEntryIndex = table.Entries.GetSize(); nEntryIndex > 0; --nEntryIndex)
		{
			EngineServiceEntry* pEntry = table.Entries[nEntryIndex - 1];
			if (pEntry == nullptr || pEntry->bIsDestroyed || pEntry->pService == nullptr)
			{
				continue;
			}

			if (pEntry->pDestroyService != nullptr)
			{
				pEntry->pDestroyService(pEntry->pService);
			}
			pEntry->pService = nullptr;
			pEntry->bIsDestroyed = true;
			++uDestroyedCount;
		}

		if (uDestroyedCount != uLiveCount)
		{
			// Only a service that was created without a service object can differ,
			// and that was already reported where it happened.
			LOG_DEBUG(kLogTag, "{} of {} engine service(s) were destroyed.", uDestroyedCount, uLiveCount);
		}
	}

	uint32 EngineServices::GetLiveServiceCount()
	{
		ServiceTable& table = GetServiceTable();
		std::lock_guard<std::mutex> lock(table.Mutex);

		uint32 uLiveCount = 0;
		for (size_t nEntryIndex = 0; nEntryIndex < table.Entries.GetSize(); ++nEntryIndex)
		{
			const EngineServiceEntry* pEntry = table.Entries[nEntryIndex];
			if (pEntry != nullptr && !pEntry->bIsDestroyed && pEntry->pService != nullptr)
			{
				++uLiveCount;
			}
		}
		return uLiveCount;
	}

	bool EngineServices::HasShutDown()
	{
		return GetServiceTable().bHasShutDown;
	}
}
