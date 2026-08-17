#pragma once

#include <cstddef>
#include <cstdint>

#include "Engine/Core/Core.h"

namespace Vsp
{
	namespace AllocatorDetail
	{
		// Small header prepended to every allocation. Free() reads it in O(1)
		// to find the backend that owns the allocation.
		struct AllocationHeader
		{
			uint64_t uPayloadSize;    // Requested payload bytes.
			uint8_t eBackendId;       // Which backend allocated it (see BackendId).
			uint8_t bIsFreed;         // Used by the linear allocator's frontier reclaim.
		};

		enum class BackendId : uint8_t
		{
			SizeClass = 0,   // Fixed-size pool allocator.
			Linear = 1,      // Chained-block linear allocator.
			System = 2,      // Direct operating-system allocation.
		};

		// Geometry shared by every backend: payloads sit 16 bytes after the
		// allocation base, which keeps them 16-byte aligned.
		static constexpr size_t k_nAllocationHeaderSize = 16;
		static constexpr size_t k_nSlotFooterSize = sizeof(size_t);
		static constexpr size_t k_nSlotAlignment = 16;
		static constexpr size_t k_nMinimumSlotSize = k_nAllocationHeaderSize + k_nSlotFooterSize;
	}

	// -------------------------------------------------------------------------
	// PoolAllocator
	// -------------------------------------------------------------------------
	// Fixed-size-slot pool: every allocation has exactly the same payload size,
	// so slots are recycled through an intrusive free list in O(1) and there is
	// no external fragmentation at all. Memory comes from slabs carved out of
	// contiguous OS allocations and grows on demand. Single-threaded; Free()
	// performs no validation for speed.
	// -------------------------------------------------------------------------
	class PoolAllocator
	{
	public:
		using SizeType = size_t;

		PoolAllocator();
		~PoolAllocator();

		// A pool owns its slabs, so it cannot be copied.
		PoolAllocator(const PoolAllocator&) = delete;
		PoolAllocator& operator=(const PoolAllocator&) = delete;

		PoolAllocator(PoolAllocator&& Other);
		PoolAllocator& operator=(PoolAllocator&& Other);

		// Sets the payload size of every slot. Must be called before the pool
		// is used.
		void Initialize(SizeType nSlotPayloadSize);

		void* Allocate();

		// Returns a slot to the free list. The pointer must have come from
		// this pool's Allocate().
		void Free(void* pPayload);

		// Releases every slab; all outstanding pointers become invalid.
		void Reset();

		SizeType GetLiveCount() const;
		SizeType GetSlotPayloadSize() const;

	private:
		void GrowSlab();
		void ReleaseSlabs();
		void MoveFrom(PoolAllocator& Other);

		// Free-list link stored inside a free slot's header zone.
		struct FreeSlot
		{
			FreeSlot* pNextFreeSlot;
		};

		// 16-byte header in front of every slab, so slots stay 16-byte aligned.
		struct SlabHeader
		{
			SlabHeader* pNextSlab;
			SizeType nSlabSize;
		};

		FreeSlot* m_pFreeListHead = nullptr;
		SlabHeader* m_pFirstSlab = nullptr;
		SizeType m_nSlotPayloadSize = 0;
		SizeType m_nSlotSize = 0;
		SizeType m_nLiveCount = 0;
	};

	// -------------------------------------------------------------------------
	// SizeClassAllocator
	// -------------------------------------------------------------------------
	// Segregated small-object allocator: a fixed ladder of PoolAllocators, one
	// per size class. Every request is rounded up to the next class, which
	// bounds internal fragmentation and keeps allocation/free at O(1) - the
	// classic way to fight fragmentation for small objects.
	// -------------------------------------------------------------------------
	class SizeClassAllocator
	{
	public:
		using SizeType = size_t;

		// 16 .. 1024 bytes: geometric-ish ladder, all multiples of the slot
		// alignment where it matters.
		static constexpr SizeType k_nSizeClassCount = 13;

		SizeClassAllocator();

		// The pool array cannot be copied; moving transfers the slabs.
		SizeClassAllocator(const SizeClassAllocator&) = delete;
		SizeClassAllocator& operator=(const SizeClassAllocator&) = delete;
		SizeClassAllocator(SizeClassAllocator&& Other) = default;
		SizeClassAllocator& operator=(SizeClassAllocator&& Other) = default;

		void* Allocate(SizeType nSizeInBytes);
		void Free(void* pPayload);
		void Reset();

		SizeType GetLiveCount() const;

		// The size class that would serve nSizeInBytes.
		static SizeType GetSizeClass(SizeType nSizeInBytes);

		// Index of the pool serving the given size class.
		static SizeType GetClassIndex(SizeType nSizeClass);

	private:
		PoolAllocator m_Pools[k_nSizeClassCount];
	};

	// -------------------------------------------------------------------------
	// LinearAllocator
	// -------------------------------------------------------------------------
	// Bump allocator over a chain of contiguous blocks. Allocating is a single
	// pointer bump (O(1), one small header per slot), and a full block simply
	// grows a new, larger contiguous block - outstanding buffers are never
	// copied. Free() reclaims the frontier of the block that owns the slot
	// (LIFO-friendly), so scratch-style usage has zero fragmentation.
	// -------------------------------------------------------------------------
	class LinearAllocator
	{
	public:
		using SizeType = size_t;

		LinearAllocator();
		~LinearAllocator();

		// A linear allocator owns its block chain, so it cannot be copied.
		LinearAllocator(const LinearAllocator&) = delete;
		LinearAllocator& operator=(const LinearAllocator&) = delete;

		LinearAllocator(LinearAllocator&& Other);
		LinearAllocator& operator=(LinearAllocator&& Other);

		// Hints the size of the first block; the chain still grows beyond it.
		void SetInitialBlockSize(SizeType nInitialBlockSize);

		void* Allocate(SizeType nSizeInBytes);
		void Free(void* pPayload);

		// Releases every block; all outstanding pointers become invalid.
		void Reset();

		bool ContainsAddress(const void* pAddress) const;

		// Total bytes reserved across the whole block chain.
		SizeType GetTotalBlockByteCount() const;

		SizeType GetLiveCount() const;

	private:
		// 32-byte header in front of every block, so slots stay 16-byte aligned.
		struct BlockHeader
		{
			BlockHeader* pNextBlock;
			SizeType nBlockSize;   // Bytes available for slots in this block.
			char* pFrontier;       // One past the last allocated slot in this block.
			uint64_t uPadding;
		};

		void GrowBlock(SizeType nRequiredSlotSize);
		void ReclaimBlockFrontier(BlockHeader* pBlock);
		BlockHeader* FindBlock(const void* pPayload) const;
		void ReleaseBlocks();
		void MoveFrom(LinearAllocator& Other);

		BlockHeader* m_pFirstBlock = nullptr;
		BlockHeader* m_pCurrentBlock = nullptr;
		char* m_pCurrentBlockStart = nullptr;
		char* m_pCurrentBlockEnd = nullptr;
		char* m_pFrontier = nullptr;
		SizeType m_nInitialBlockSize = 0;
		SizeType m_nLastBlockSize = 0;
		SizeType m_nLiveCount = 0;
	};

	// -------------------------------------------------------------------------
	// SystemAllocator
	// -------------------------------------------------------------------------
	// Straight to the operating system (_aligned_malloc/_aligned_free). Used
	// for allocations too large to make sense inside a block; the OS manages
	// their layout, so they are not guaranteed contiguous with anything else.
	// -------------------------------------------------------------------------
	class SystemAllocator
	{
	public:
		using SizeType = size_t;

		SystemAllocator() {}
		~SystemAllocator() {}

		void* Allocate(SizeType nSizeInBytes);
		void Free(void* pPayload);

		SizeType GetLiveCount() const;

	private:
		SizeType m_nLiveCount = 0;
	};

	// -------------------------------------------------------------------------
	// Allocator
	// -------------------------------------------------------------------------
	// Unified allocator facade. It owns several special-purpose allocators and
	// picks the right one for every request:
	//
	//   - payload <= size-class limit  -> SizeClassAllocator (fixed-size pools;
	//     O(1), bounded internal fragmentation, zero external fragmentation)
	//   - payload <= linear limit      -> LinearAllocator (O(1) bump over a
	//     chain of contiguous blocks; LIFO reclaim keeps scratch usage
	//     fragmentation-free)
	//   - everything larger            -> SystemAllocator (the OS decides)
	//
	// Every allocation is preceded by a small 16-byte header, so Free() finds
	// the owning backend in O(1) - no search, no size tables on the free path.
	//
	// Constructing with a single block size selects the contiguous mode: every
	// allocation, whatever its size, is served from the linear block chain, so
	// all allocations live in contiguous blocks owned by this allocator.
	//
	// All payloads are 16-byte aligned. This class never uses C++ exceptions
	// (failed allocations return nullptr after DEBUG_BREAK()) and is
	// single-threaded; Free() performs no validation for speed.
	// -------------------------------------------------------------------------
#pragma warning(push)
#pragma warning(disable : 4251)   // Backend members of the exported facade class.
	class RUNTIME_API Allocator
	{
	public:
		using SizeType = size_t;

		static constexpr SizeType k_nMaximumAlignment = 16;
		static constexpr SizeType k_nDefaultSizeClassLimit = 1024;
		static constexpr SizeType k_nDefaultLinearLimit = 64 * 1024;

		// Hybrid mode: sizes are routed by the two limits above.
		explicit Allocator(SizeType nSizeClassLimit = k_nDefaultSizeClassLimit, SizeType nLinearLimit = k_nDefaultLinearLimit);

		// Contiguous mode: one linear block chain serves every allocation.
		static Allocator CreateContiguous(SizeType nContiguousBlockSize);

		// The allocator owns its backend state, so it cannot be copied.
		Allocator(const Allocator&) = delete;
		Allocator& operator=(const Allocator&) = delete;

		Allocator(Allocator&& Other);
		Allocator& operator=(Allocator&& Other);
		~Allocator();

		// -------- Allocation --------
		void* Allocate(SizeType nSizeInBytes);

		// Allocates nSizeInBytes bytes aligned to nAlignment (1, 2, 4, 8 or 16;
		// anything else is clamped to 16 after DEBUG_BREAK()). Every backend
		// already hands out 16-byte aligned payloads.
		void* AllocateAligned(SizeType nSizeInBytes, SizeType nAlignment);

		// Releases a pointer previously returned by Allocate/AllocateAligned.
		void Free(void* pMemory);

		// Resets the pool and linear backends; every outstanding pointer except
		// system allocations becomes invalid.
		void Reset();

		// -------- Observers --------
		// True when constructed with a block size (all allocations come from
		// the linear block chain); false in hybrid mode.
		bool IsContiguousLayoutMode() const;

		// Bytes reserved by the linear backend across its block chain; grows as
		// blocks are added. 0 until the first block is created.
		SizeType GetBlockSize() const;

		SizeType GetSizeClassLimit() const;
		SizeType GetLinearLimit() const;

		// True when pAddress lies inside one of the linear backend's blocks.
		bool ContainsAddress(const void* pAddress) const;

		SizeType GetLiveAllocationCount() const;
		SizeType GetLivePoolAllocationCount() const;
		SizeType GetLiveLinearAllocationCount() const;
		SizeType GetLiveSystemAllocationCount() const;

		bool IsEmpty() const;

	private:
		void MoveFrom(Allocator& Other);
		static bool IsPowerOfTwo(SizeType nValue);

		SizeType m_nSizeClassLimit = k_nDefaultSizeClassLimit;
		SizeType m_nLinearLimit = k_nDefaultLinearLimit;
		bool m_bIsContiguousLayoutMode = false;
		SizeClassAllocator m_SizeClassAllocator;
		LinearAllocator m_LinearAllocator;
		SystemAllocator m_SystemAllocator;
	};
#pragma warning(pop)
}
