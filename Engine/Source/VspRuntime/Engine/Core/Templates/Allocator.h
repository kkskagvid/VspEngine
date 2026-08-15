#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <malloc.h>

#include "Engine/Core/Core.h"

namespace Vsp
{
	// -------------------------------------------------------------------------
	// Allocator
	// -------------------------------------------------------------------------
	// Raw-memory allocator with two selectable layouts:
	//
	//   Contiguous layout (constructor block size > 0) - C# GC style
	//   generational mark-compact:
	//
	//     gen2Start   gen1Start   gen0Start    smallFrontier      largeFrontier      blockEnd
	//        |           |           |              |                  |                |
	//        v           v           v              v                  v                v
	//     [ gen2 ][ gen1 ][ gen0 ][      free space      ][ large objects (grow down) ]
	//
	//     The generational area (low addresses) holds small objects (size <=
	//     the small-allocation limit) in three generations, mirroring the C# GC
	//     ephemeral segment: gen2 is the oldest generation at the bottom, gen1
	//     holds survivors of gen0 collections, and gen0 holds the newest
	//     objects right below the allocation pointer. Allocating a small object
	//     simply bumps the gen0 frontier upward.
	//
	//     The large-object area (high addresses) holds objects above the limit
	//     and grows downward. Like the C# large object heap, its objects are
	//     never compacted: freeing a large object moves the area frontier back
	//     and the next allocation reuses that memory.
	//
	//     The two areas grow toward each other and are fully independent:
	//     allocations on one side never block or depend on the other side.
	//
	//     Free() marks the slot freed and, when the freed slot sits at the
	//     frontier of its area, moves the frontier pointer back immediately
	//     (the memory is not zeroed or scribbled - the next allocation reuses
	//     and overwrites it). Slots freed deeper inside an area stay reserved
	//     until the frontier reaches them or a collection reclaims them.
	//
	//     CollectGeneration(N) performs a C#-style mark-compact collection of
	//     generations 0..N (collecting generation N also collects the younger
	//     generations, exactly like the C# GC):
	//       - Mark phase: the caller marks every reachable small object with
	//         MarkReachable() BEFORE the collection. Like the rest of this
	//         allocator there is no tracing: nothing traverses the stack or
	//         analyzes references. Whatever was freed with Free() or simply
	//         left unmarked is garbage.
	//       - Sweep phase: dead slots are discarded.
	//       - Compact phase: surviving slots slide down toward the low end of
	//         their generation (sliding compaction, like the C# mark-compact
	//         collector); afterwards they are promoted to the next generation
	//         (gen0 -> gen1 -> gen2) and the allocation pointer moves back, so
	//         the reclaimed space is reused and overwritten by later
	//         allocations.
	//       Surviving objects MOVE during compaction: their addresses change,
	//       so callers must re-acquire them after the collection.
	//
	//     When the generational area is exhausted, Allocate() first runs an
	//     automatic full mark-compact (the same rules as CollectGeneration(2):
	//     only marked-and-not-freed slots survive) and then retries; it fails
	//     with DEBUG_BREAK() and nullptr only when compaction cannot free
	//     enough room. The large-object area is never compacted, so exhausting
	//     it can only be relieved by freeing its frontier slots.
	//
	//     Large objects never take part in collections (LOH-like): they are
	//     reclaimed only by explicit Free() at the area frontier.
	//
	//   OS layout (constructor block size == 0, the default):
	//     Every allocation goes straight to _aligned_malloc and Free() goes to
	//     _aligned_free, so memory is laid out however the operating system
	//     decides. MarkReachable()/CollectGeneration() are only meaningful in
	//     the contiguous layout.
	//
	// The allocator hands out raw memory only: it never constructs or destroys
	// objects, so callers are responsible for constructors/destructors.
	// It never uses C++ exceptions - failed allocations return nullptr after
	// DEBUG_BREAK(), and misuse (bad alignment, double free, foreign pointers)
	// is reported with DEBUG_BREAK(). Single-threaded by design.
	// -------------------------------------------------------------------------
	class RUNTIME_API Allocator
	{
	public:
		using SizeType = size_t;

		// Slot shapes:
		//   Small slot: [ header zone (16) ][ payload ][ footer (8) ][ tail padding ]
		//               The small frontier is the slot END, so the footer sits
		//               at frontier - 8.
		//   Large slot: [ header zone (16) ][ footer (8) ][ padding ][ payload ][ tail padding ]
		//               The large frontier is the slot START, so the footer
		//               sits at frontier + 16 and the payload at frontier + 32.
		// Slot sizes are multiples of 16, so payloads are always 16-byte
		// aligned. The footer mirrors the slot size at a fixed offset from its
		// frontier, which lets each frontier walk back over freed slots without
		// knowing payload sizes in advance.
		static constexpr SizeType k_nHeaderZoneSize = 16;
		static constexpr SizeType k_nSlotFooterSize = sizeof(SizeType);
		static constexpr SizeType k_nSlotAlignment = 16;
		static constexpr SizeType k_nMinimumSlotSize = k_nHeaderZoneSize + k_nSlotFooterSize;

		// Large-slot payload offset: header zone (16) + footer (8) + padding (8).
		static constexpr SizeType k_nLargePayloadOffset = 32;

		// Highest alignment the allocator supports; it matches
		// alignof(std::max_align_t) and the slot alignment above.
		static constexpr SizeType k_nMaximumAlignment = 16;

		// -------- Construction --------
		// nContiguousBlockSize > 0 selects the contiguous layout and reserves a
		// block of that size; 0 selects the OS layout. nSmallAllocationLimit is
		// the size boundary between the generational area and the large-object
		// area.
		explicit Allocator(SizeType nContiguousBlockSize = 0, SizeType nSmallAllocationLimit = 256);

		// The allocator owns its block, so it cannot be copied.
		Allocator(const Allocator&) = delete;
		Allocator& operator=(const Allocator&) = delete;

		Allocator(Allocator&& Other);
		Allocator& operator=(Allocator&& Other);
		~Allocator();

		// -------- Allocation --------
		// Allocates nSizeInBytes bytes with k_nMaximumAlignment alignment.
		void* Allocate(SizeType nSizeInBytes);

		// Allocates nSizeInBytes bytes aligned to nAlignment (1, 2, 4, 8 or 16;
		// anything else is clamped to 16 after DEBUG_BREAK()).
		void* AllocateAligned(SizeType nSizeInBytes, SizeType nAlignment);

		// Releases a pointer previously returned by Allocate/AllocateAligned.
		// The slot is marked freed and the frontier of its area moves back when
		// possible; the memory is then handed out and overwritten by the next
		// allocation. No destructor is invoked.
		void Free(void* pMemory);

		// -------- Mark phase (C# GC-style, but caller driven) --------
		// Marks the slot containing pMemory as reachable for the next
		// CollectGeneration() call. The allocator itself never traces the stack
		// or analyzes references: the caller is the "GC roots" provider and
		// must mark every object that should survive the collection.
		void MarkReachable(void* pMemory);

		// Clears every reachability mark in the block. Marks are also cleared
		// on surviving slots during a collection.
		void ClearReachabilityMarks();

		// -------- Collections (mark-compact) --------
		// Sweeps and compacts generations 0..uGeneration of the generational
		// (small-object) area: dead slots are discarded, reachable slots slide
		// toward the low end of their generation and are promoted to the next
		// generation, and the allocation pointer moves back over the reclaimed
		// space. A collection of generation N also collects the younger
		// generations, like the C# GC. Surviving pointers MOVE - re-acquire
		// them after the call. Large objects are never collected or moved.
		void CollectGeneration(uint8_t uGeneration);

		// -------- Reset --------
		// Resets the contiguous layout to "nothing allocated": all generation
		// watermarks and frontiers move back and every outstanding pointer
		// becomes invalid. The block itself stays reserved and the next
		// allocation overwrites the previous contents. No-op for the OS layout
		// (apart from resetting the allocation counter).
		void Reset();

		// -------- Observers --------
		// True when the allocator reserves one contiguous block and serves all
		// allocations from it; false when allocations go straight to the OS.
		bool IsContiguousLayoutMode() const;

		// Total usable bytes of the reserved block; 0 in OS layout mode.
		SizeType GetBlockSize() const;

		// True when pAddress lies inside the reserved block.
		bool ContainsAddress(const void* pAddress) const;

		// Bytes handed out but not yet reclaimed; 0 in OS layout mode.
		SizeType GetUsedByteCount() const;

		// Bytes still available for new allocations; 0 in OS layout mode.
		SizeType GetAvailableByteCount() const;

		bool IsEmpty() const;

		SizeType GetLiveAllocationCount() const;
		SizeType GetLiveSmallAllocationCount() const;
		SizeType GetLiveLargeAllocationCount() const;

		// Generation range starts (C# GC heap layout): gen0 is the youngest
		// generation, directly below the allocation pointer; gen2 is the oldest
		// one at the block base. All three return nullptr in OS layout mode.
		const void* GetGen0StartAddress() const;
		const void* GetGen1StartAddress() const;
		const void* GetGen2StartAddress() const;

		// Size boundary between the generational (small-object) area and the
		// large-object area.
		SizeType GetSmallAllocationLimit() const;
		void SetSmallAllocationLimit(SizeType nSmallAllocationLimit);

		// True when a request of nSizeInBytes would be served from the
		// generational (small-object) area.
		bool IsSmallAllocation(SizeType nSizeInBytes) const;

	private:
		// Header written at the start of every contiguous-layout slot. Free()
		// recovers it by subtracting k_nHeaderZoneSize from the payload pointer.
		struct AllocationHeader
		{
			SizeType nSlotSize;         // Total slot bytes: header zone + payload + footer.
			uint8_t bIsSmallAllocation; // 1 = generational area, 0 = large-object area.
			uint8_t bIsFreed;           // Set by Free(); the slot is garbage for collections.
			uint8_t bIsReachable;       // Set by MarkReachable(); survives the next collection.
		};

		// -------- Contiguous layout --------
		void* AllocateFromContiguousBlock(SizeType nSizeInBytes, bool bIsSmallAllocation);
		void FreeFromContiguousBlock(void* pMemory);
		void ReclaimSmallFrontier();
		void ReclaimLargeFrontier();

		// Mark-compact internals.
		char* SweepAndCompactRange(char* pRangeStart, char* pRangeEnd);
		void RecountLiveSlots();

		// -------- OS layout --------
		void* AllocateFromOs(SizeType nSizeInBytes, SizeType nAlignment);
		void FreeFromOs(void* pMemory);

		// -------- Block management --------
		void InitializeContiguousBlock(SizeType nContiguousBlockSize);
		void ReleaseContiguousBlock();
		void ResetObjectWatermarks();
		void MoveFrom(Allocator& Other);

		// -------- Address helpers --------
		static SizeType AlignUpValue(SizeType nValue, SizeType nAlignment);
		static SizeType AlignDownValue(SizeType nValue, SizeType nAlignment);
		static char* AlignUpAddress(char* pAddress, SizeType nAlignment);
		static bool IsPowerOfTwo(SizeType nValue);

	private:
		char* m_pBlockAllocation = nullptr; // Raw malloc result; the aligned block starts at m_pBlockBase.
		char* m_pBlockBase = nullptr;       // Lowest usable address of the reserved block.
		char* m_pBlockEnd = nullptr;        // One past the highest usable address.
		SizeType m_nBlockSize = 0;          // Usable bytes between m_pBlockBase and m_pBlockEnd.

		// Generational area (small objects, low addresses, growing up). The
		// ranges mirror the C# GC ephemeral segment: gen2 = [gen2Start, gen1Start),
		// gen1 = [gen1Start, gen0Start), gen0 = [gen0Start, smallFrontier).
		char* m_pGen2Start = nullptr;     // Oldest generation; stays at the block base.
		char* m_pGen1Start = nullptr;     // Start of the gen1 range.
		char* m_pGen0Start = nullptr;     // Start of the gen0 range.
		char* m_pSmallFrontier = nullptr; // Gen0 allocation pointer; end of the generational area.

		// Large-object area (high addresses, growing down; never compacted).
		char* m_pLargeFrontier = nullptr; // Start of the newest large slot.

		SizeType m_nSmallAllocationLimit = 256;
		SizeType m_nLiveAllocationCount = 0;
		SizeType m_nLiveSmallAllocationCount = 0;
		SizeType m_nLiveLargeAllocationCount = 0;
		bool m_bIsContiguousLayout = false;
	};
}
