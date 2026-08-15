#include "RuntimePCH.h"

#include "Engine/Core/Templates/Allocator.h"

namespace Vsp
{
	// =========================================================================
	// Construction
	// =========================================================================

	Allocator::Allocator(SizeType nContiguousBlockSize, SizeType nSmallAllocationLimit)
		: m_nSmallAllocationLimit(nSmallAllocationLimit)
	{
		InitializeContiguousBlock(nContiguousBlockSize);
	}

	Allocator::Allocator(Allocator&& Other)
	{
		MoveFrom(Other);
	}

	Allocator& Allocator::operator=(Allocator&& Other)
	{
		if (this != &Other)
		{
			ReleaseContiguousBlock();
			MoveFrom(Other);
		}
		return *this;
	}

	Allocator::~Allocator()
	{
		ReleaseContiguousBlock();
	}

	// =========================================================================
	// Allocation
	// =========================================================================

	void* Allocator::Allocate(SizeType nSizeInBytes)
	{
		return AllocateAligned(nSizeInBytes, k_nMaximumAlignment);
	}

	void* Allocator::AllocateAligned(SizeType nSizeInBytes, SizeType nAlignment)
	{
		if (nSizeInBytes == 0)
		{
			DEBUG_BREAK();
			nSizeInBytes = 1;
		}

		if (nAlignment == 0 || nAlignment > k_nMaximumAlignment || !IsPowerOfTwo(nAlignment))
		{
			// Unsupported alignment: clamp to the maximum the allocator can give.
			DEBUG_BREAK();
			nAlignment = k_nMaximumAlignment;
		}

		if (IsContiguousLayoutMode())
		{
			return AllocateFromContiguousBlock(nSizeInBytes, IsSmallAllocation(nSizeInBytes));
		}
		return AllocateFromOs(nSizeInBytes, nAlignment);
	}

	void Allocator::Free(void* pMemory)
	{
		if (pMemory == nullptr)
		{
			DEBUG_BREAK();
			return;
		}

		if (IsContiguousLayoutMode())
		{
			FreeFromContiguousBlock(pMemory);
		}
		else
		{
			FreeFromOs(pMemory);
		}
	}

	void Allocator::MarkReachable(void* pMemory)
	{
		if (pMemory == nullptr || !IsContiguousLayoutMode() || !ContainsAddress(pMemory))
		{
			DEBUG_BREAK();
			return;
		}

		char* pPayload = static_cast<char*>(pMemory);

		// The side follows from the address, exactly like in Free().
		bool bIsSmallArea;
		if (pPayload < m_pSmallFrontier)
		{
			bIsSmallArea = true;
		}
		else if (pPayload >= m_pLargeFrontier)
		{
			bIsSmallArea = false;
		}
		else
		{
			DEBUG_BREAK();   // Address inside the free gap between the areas.
			return;
		}

		char* pSlotStart = pPayload - (bIsSmallArea ? k_nHeaderZoneSize : k_nLargePayloadOffset);
		AllocationHeader* pHeader = reinterpret_cast<AllocationHeader*>(pSlotStart);

		// Sanity: the header must describe a plausible slot inside the active
		// region of its area, and the footer must agree.
		if (pHeader->bIsSmallAllocation != (bIsSmallArea ? 1 : 0) ||
			pHeader->nSlotSize < k_nMinimumSlotSize ||
			(pHeader->nSlotSize & (k_nSlotAlignment - 1)) != 0 ||
			(bIsSmallArea
				? pSlotStart + pHeader->nSlotSize > m_pSmallFrontier ||
				*reinterpret_cast<SizeType*>(pSlotStart + pHeader->nSlotSize - k_nSlotFooterSize) != pHeader->nSlotSize
				: pSlotStart < m_pLargeFrontier || pSlotStart + pHeader->nSlotSize > m_pBlockEnd ||
				*reinterpret_cast<SizeType*>(pSlotStart + k_nHeaderZoneSize) != pHeader->nSlotSize))
		{
			DEBUG_BREAK();
			return;
		}

		pHeader->bIsReachable = 1;
	}

	void Allocator::ClearReachabilityMarks()
	{
		if (!IsContiguousLayoutMode())
		{
			DEBUG_BREAK();
			return;
		}

		// Generational area.
		char* pCursor = m_pGen2Start;
		char* pAreaEnd = m_pSmallFrontier;
		while (pCursor < pAreaEnd)
		{
			AllocationHeader* pHeader = reinterpret_cast<AllocationHeader*>(pCursor);
			pHeader->bIsReachable = 0;
			pCursor += pHeader->nSlotSize;
		}

		// Large-object area.
		pCursor = m_pLargeFrontier;
		pAreaEnd = m_pBlockEnd;
		while (pCursor < pAreaEnd)
		{
			AllocationHeader* pHeader = reinterpret_cast<AllocationHeader*>(pCursor);
			pHeader->bIsReachable = 0;
			pCursor += pHeader->nSlotSize;
		}
	}

	void Allocator::CollectGeneration(uint8_t uGeneration)
	{
		if (!IsContiguousLayoutMode())
		{
			DEBUG_BREAK();
			return;
		}
		if (uGeneration > 2)
		{
			DEBUG_BREAK();
			uGeneration = 2;
		}

		// Gen0 is always collected; surviving slots are compacted to the low
		// end of the gen0 range and promoted into gen1 by moving the gen0
		// watermark above them.
		char* pNewGen0Start = SweepAndCompactRange(m_pGen0Start, m_pSmallFrontier);
		m_pGen0Start = pNewGen0Start;
		m_pSmallFrontier = pNewGen0Start;

		// Collecting gen1 (or gen2) also compacts gen1; its survivors become gen2.
		if (uGeneration >= 1)
		{
			char* pNewGen1Start = SweepAndCompactRange(m_pGen1Start, m_pGen0Start);
			m_pGen1Start = pNewGen1Start;
			m_pGen0Start = pNewGen1Start;
			m_pSmallFrontier = pNewGen1Start;
		}

		// A gen2 collection compacts gen2; survivors stay gen2 at the block base.
		if (uGeneration >= 2)
		{
			char* pNewGen2End = SweepAndCompactRange(m_pGen2Start, m_pGen1Start);
			m_pGen1Start = pNewGen2End;
			m_pGen0Start = pNewGen2End;
			m_pSmallFrontier = pNewGen2End;
		}

		RecountLiveSlots();
	}

	void Allocator::Reset()
	{
		if (IsContiguousLayoutMode())
		{
			ResetObjectWatermarks();
		}
		m_nLiveAllocationCount = 0;
		m_nLiveSmallAllocationCount = 0;
		m_nLiveLargeAllocationCount = 0;
	}

	// =========================================================================
	// Observers
	// =========================================================================

	bool Allocator::IsContiguousLayoutMode() const
	{
		return m_bIsContiguousLayout;
	}

	Allocator::SizeType Allocator::GetBlockSize() const
	{
		return m_nBlockSize;
	}

	bool Allocator::ContainsAddress(const void* pAddress) const
	{
		if (!m_bIsContiguousLayout)
		{
			return false;
		}
		uintptr_t uAddress = reinterpret_cast<uintptr_t>(pAddress);
		return uAddress >= reinterpret_cast<uintptr_t>(m_pBlockBase) &&
			uAddress < reinterpret_cast<uintptr_t>(m_pBlockEnd);
	}

	Allocator::SizeType Allocator::GetUsedByteCount() const
	{
		if (!IsContiguousLayoutMode())
		{
			return 0;
		}
		return static_cast<SizeType>((m_pSmallFrontier - m_pBlockBase) + (m_pBlockEnd - m_pLargeFrontier));
	}

	Allocator::SizeType Allocator::GetAvailableByteCount() const
	{
		if (!IsContiguousLayoutMode())
		{
			return 0;
		}
		return static_cast<SizeType>(m_pLargeFrontier - m_pSmallFrontier);
	}

	bool Allocator::IsEmpty() const
	{
		return m_nLiveAllocationCount == 0;
	}

	Allocator::SizeType Allocator::GetLiveAllocationCount() const
	{
		return m_nLiveAllocationCount;
	}

	Allocator::SizeType Allocator::GetLiveSmallAllocationCount() const
	{
		return m_nLiveSmallAllocationCount;
	}

	Allocator::SizeType Allocator::GetLiveLargeAllocationCount() const
	{
		return m_nLiveLargeAllocationCount;
	}

	const void* Allocator::GetGen0StartAddress() const
	{
		return m_pGen0Start;
	}

	const void* Allocator::GetGen1StartAddress() const
	{
		return m_pGen1Start;
	}

	const void* Allocator::GetGen2StartAddress() const
	{
		return m_pGen2Start;
	}

	Allocator::SizeType Allocator::GetSmallAllocationLimit() const
	{
		return m_nSmallAllocationLimit;
	}

	void Allocator::SetSmallAllocationLimit(SizeType nSmallAllocationLimit)
	{
		m_nSmallAllocationLimit = nSmallAllocationLimit;
	}

	bool Allocator::IsSmallAllocation(SizeType nSizeInBytes) const
	{
		return nSizeInBytes <= m_nSmallAllocationLimit;
	}

	// =========================================================================
	// Contiguous layout
	// =========================================================================

	void* Allocator::AllocateFromContiguousBlock(SizeType nSizeInBytes, bool bIsSmallAllocation)
	{
		// Slot: 16-byte header zone + payload + 8-byte footer, rounded up so
		// every slot and every frontier stay 16-byte aligned.
		SizeType nSlotSize = AlignUpValue(k_nHeaderZoneSize + k_nSlotFooterSize + nSizeInBytes, k_nSlotAlignment);

		if (bIsSmallAllocation)
		{
			// Small objects join gen0 by bumping the allocation pointer.
			char* pSlotStart = m_pSmallFrontier;
			if (pSlotStart + nSlotSize > m_pLargeFrontier)
			{
				// The generational area is exhausted: run an automatic full
				// mark-compact to reclaim garbage, then try again. The same
				// rules as CollectGeneration(2) apply - only slots that were
				// marked reachable and not freed survive; everything else is
				// garbage and its space is reused.
				CollectGeneration(2);
				pSlotStart = m_pSmallFrontier;
				if (pSlotStart + nSlotSize > m_pLargeFrontier)
				{
					DEBUG_BREAK();
					return nullptr;
				}
			}

			AllocationHeader* pHeader = reinterpret_cast<AllocationHeader*>(pSlotStart);
			pHeader->nSlotSize = nSlotSize;
			pHeader->bIsSmallAllocation = 1;
			pHeader->bIsFreed = 0;
			pHeader->bIsReachable = 0;
			*reinterpret_cast<SizeType*>(pSlotStart + nSlotSize - k_nSlotFooterSize) = nSlotSize;

			m_pSmallFrontier = pSlotStart + nSlotSize;
			m_nLiveSmallAllocationCount++;
			m_nLiveAllocationCount++;
			return pSlotStart + k_nHeaderZoneSize;
		}

		// Large objects grow down from the high end of the block. The large
		// area is never compacted, so exhausting it cannot be relieved by a
		// collection; Free() at the area frontier is the only way to reclaim it.
		SizeType nLargeSlotSize = AlignUpValue(k_nLargePayloadOffset + nSizeInBytes, k_nSlotAlignment);
		char* pSlotStart = m_pLargeFrontier - nLargeSlotSize;
		if (pSlotStart < m_pSmallFrontier)
		{
			DEBUG_BREAK();
			return nullptr;
		}

		AllocationHeader* pHeader = reinterpret_cast<AllocationHeader*>(pSlotStart);
		pHeader->nSlotSize = nLargeSlotSize;
		pHeader->bIsSmallAllocation = 0;
		pHeader->bIsFreed = 0;
		pHeader->bIsReachable = 0;
		*reinterpret_cast<SizeType*>(pSlotStart + k_nHeaderZoneSize) = nLargeSlotSize;

		m_pLargeFrontier = pSlotStart;
		m_nLiveLargeAllocationCount++;
		m_nLiveAllocationCount++;
		return pSlotStart + k_nLargePayloadOffset;
	}

	void Allocator::FreeFromContiguousBlock(void* pMemory)
	{
		if (!ContainsAddress(pMemory))
		{
			DEBUG_BREAK();
			return;
		}

		char* pPayload = static_cast<char*>(pMemory);

		// The side follows from the address: the generational area lies below
		// the small frontier, the large-object area at or above the large
		// frontier, and the two never overlap.
		bool bIsSmallArea;
		if (pPayload < m_pSmallFrontier)
		{
			bIsSmallArea = true;
		}
		else if (pPayload >= m_pLargeFrontier)
		{
			bIsSmallArea = false;
		}
		else
		{
			DEBUG_BREAK();   // Address inside the free gap between the areas.
			return;
		}

		char* pSlotStart = pPayload - (bIsSmallArea ? k_nHeaderZoneSize : k_nLargePayloadOffset);
		AllocationHeader* pHeader = reinterpret_cast<AllocationHeader*>(pSlotStart);

		// Sanity: the header must describe a plausible, not-yet-freed slot on
		// the side implied by the address, and header/footer must agree.
		if (pHeader->bIsFreed != 0 ||
			pHeader->bIsSmallAllocation != (bIsSmallArea ? 1 : 0) ||
			pHeader->nSlotSize < k_nMinimumSlotSize ||
			(pHeader->nSlotSize & (k_nSlotAlignment - 1)) != 0)
		{
			DEBUG_BREAK();
			return;
		}

		if (bIsSmallArea)
		{
			if (pSlotStart + pHeader->nSlotSize > m_pSmallFrontier ||
				*reinterpret_cast<SizeType*>(pSlotStart + pHeader->nSlotSize - k_nSlotFooterSize) != pHeader->nSlotSize)
			{
				DEBUG_BREAK();
				return;
			}
			m_nLiveSmallAllocationCount--;
		}
		else
		{
			if (pSlotStart < m_pLargeFrontier || pSlotStart + pHeader->nSlotSize > m_pBlockEnd ||
				*reinterpret_cast<SizeType*>(pSlotStart + k_nHeaderZoneSize) != pHeader->nSlotSize)
			{
				DEBUG_BREAK();
				return;
			}
			m_nLiveLargeAllocationCount--;
		}

		pHeader->bIsFreed = 1;
		m_nLiveAllocationCount--;

		// Move the frontier pointer of the freed side; subsequent allocations
		// reuse and overwrite the reclaimed memory.
		if (bIsSmallArea)
		{
			ReclaimSmallFrontier();
		}
		else
		{
			ReclaimLargeFrontier();
		}
	}

	void Allocator::ReclaimSmallFrontier()
	{
		// Pop the gen0 allocation pointer back over freed slots at the top of
		// the generational area. Only gen0 slots can be popped: gen1/gen2
		// reclamation belongs to CollectGeneration().
		while (m_pSmallFrontier > m_pGen0Start)
		{
			SizeType nSlotSize = *reinterpret_cast<SizeType*>(m_pSmallFrontier - k_nSlotFooterSize);
			if (nSlotSize < k_nMinimumSlotSize ||
				nSlotSize > static_cast<SizeType>(m_pSmallFrontier - m_pGen0Start))
			{
				DEBUG_BREAK();
				break;
			}
			AllocationHeader* pHeader = reinterpret_cast<AllocationHeader*>(m_pSmallFrontier - nSlotSize);
			if (pHeader->bIsSmallAllocation == 0 || pHeader->bIsFreed == 0)
			{
				break;
			}
			m_pSmallFrontier -= nSlotSize;
		}
	}

	void Allocator::ReclaimLargeFrontier()
	{
		// Walk the large frontier back over freed slots at the top of the
		// large-object area. The frontier is the slot START, so the footer of
		// the newest large slot sits right after it.
		while (m_pLargeFrontier < m_pBlockEnd)
		{
			SizeType nSlotSize = *reinterpret_cast<SizeType*>(m_pLargeFrontier + k_nHeaderZoneSize);
			if (nSlotSize < k_nMinimumSlotSize ||
				nSlotSize > static_cast<SizeType>(m_pBlockEnd - m_pLargeFrontier))
			{
				DEBUG_BREAK();
				break;
			}
			AllocationHeader* pHeader = reinterpret_cast<AllocationHeader*>(m_pLargeFrontier);
			if (pHeader->bIsSmallAllocation != 0 || pHeader->bIsFreed == 0)
			{
				break;
			}
			m_pLargeFrontier += nSlotSize;
		}
	}

	char* Allocator::SweepAndCompactRange(char* pRangeStart, char* pRangeEnd)
	{
		// Sweep + sliding compaction over one generation range. Reachable,
		// not-freed slots are moved down toward pRangeStart (memmove handles
		// the overlap); dead slots are discarded and overwritten. Returns the
		// address just past the compacted survivors.
		char* pCursor = pRangeStart;
		char* pDestination = pRangeStart;
		while (pCursor < pRangeEnd)
		{
			AllocationHeader* pHeader = reinterpret_cast<AllocationHeader*>(pCursor);
			SizeType nSlotSize = pHeader->nSlotSize;
			if (nSlotSize < k_nMinimumSlotSize ||
				(nSlotSize & (k_nSlotAlignment - 1)) != 0 ||
				pCursor + nSlotSize > pRangeEnd)
			{
				DEBUG_BREAK();
				return pRangeEnd;
			}

			if (pHeader->bIsReachable != 0 && pHeader->bIsFreed == 0)
			{
				// Survivors keep their marks while the later stages of the same
				// collection still need them (a promoted slot is swept again as
				// part of the older generation); CollectGeneration clears every
				// mark once all stages have run.
				if (pDestination != pCursor)
				{
					memmove(pDestination, pCursor, nSlotSize);
				}
				pDestination += nSlotSize;
			}
			pCursor += nSlotSize;
		}
		return pDestination;
	}

	void Allocator::RecountLiveSlots()
	{
		// After a collection the generational area only contains survivors;
		// walk it once to refresh the counters and to end the sweep by
		// clearing every remaining reachability mark, so the next collection
		// starts from a clean marking phase.
		m_nLiveSmallAllocationCount = 0;
		char* pCursor = m_pGen2Start;
		char* pAreaEnd = m_pSmallFrontier;
		while (pCursor < pAreaEnd)
		{
			AllocationHeader* pHeader = reinterpret_cast<AllocationHeader*>(pCursor);
			SizeType nSlotSize = pHeader->nSlotSize;
			if (nSlotSize < k_nMinimumSlotSize ||
				(nSlotSize & (k_nSlotAlignment - 1)) != 0 ||
				pCursor + nSlotSize > pAreaEnd)
			{
				DEBUG_BREAK();
				break;
			}
			pHeader->bIsReachable = 0;
			m_nLiveSmallAllocationCount++;
			pCursor += nSlotSize;
		}
		m_nLiveAllocationCount = m_nLiveSmallAllocationCount + m_nLiveLargeAllocationCount;
	}

	// =========================================================================
	// OS layout
	// =========================================================================

	void* Allocator::AllocateFromOs(SizeType nSizeInBytes, SizeType nAlignment)
	{
		// _aligned_malloc expects the size to be a multiple of the alignment.
		SizeType nAlignedSize = AlignUpValue(nSizeInBytes, nAlignment);
		void* pMemory = _aligned_malloc(nAlignedSize, nAlignment);
		if (pMemory == nullptr)
		{
			DEBUG_BREAK();
			return nullptr;
		}
		m_nLiveAllocationCount++;
		return pMemory;
	}

	void Allocator::FreeFromOs(void* pMemory)
	{
		_aligned_free(pMemory);
		if (m_nLiveAllocationCount > 0)
		{
			m_nLiveAllocationCount--;
		}
	}

	// =========================================================================
	// Block management
	// =========================================================================

	void Allocator::InitializeContiguousBlock(SizeType nContiguousBlockSize)
	{
		if (nContiguousBlockSize == 0)
		{
			// OS layout mode: no block is reserved.
			return;
		}

		// Round the usable size down so the block end stays slot-aligned.
		SizeType nAlignedBlockSize = AlignDownValue(nContiguousBlockSize, k_nSlotAlignment);
		if (nAlignedBlockSize < k_nMinimumSlotSize)
		{
			DEBUG_BREAK();
			return;
		}

		char* pRawAllocation = static_cast<char*>(malloc(nAlignedBlockSize + k_nSlotAlignment));
		if (pRawAllocation == nullptr)
		{
			DEBUG_BREAK();
			return;
		}

		m_pBlockAllocation = pRawAllocation;
		m_pBlockBase = AlignUpAddress(pRawAllocation, k_nSlotAlignment);
		m_pBlockEnd = m_pBlockBase + nAlignedBlockSize;
		m_nBlockSize = nAlignedBlockSize;
		m_bIsContiguousLayout = true;
		ResetObjectWatermarks();
	}

	void Allocator::ReleaseContiguousBlock()
	{
		free(m_pBlockAllocation);
		m_pBlockAllocation = nullptr;
		m_pBlockBase = nullptr;
		m_pBlockEnd = nullptr;
		m_pGen2Start = nullptr;
		m_pGen1Start = nullptr;
		m_pGen0Start = nullptr;
		m_pSmallFrontier = nullptr;
		m_pLargeFrontier = nullptr;
		m_nBlockSize = 0;
		m_bIsContiguousLayout = false;
	}

	void Allocator::ResetObjectWatermarks()
	{
		// Empty heap: all three generation watermarks and the allocation
		// pointer collapse onto the block base; the large frontier sits at the
		// block end.
		m_pGen2Start = m_pBlockBase;
		m_pGen1Start = m_pBlockBase;
		m_pGen0Start = m_pBlockBase;
		m_pSmallFrontier = m_pBlockBase;
		m_pLargeFrontier = m_pBlockEnd;
	}

	void Allocator::MoveFrom(Allocator& Other)
	{
		m_pBlockAllocation = Other.m_pBlockAllocation;
		m_pBlockBase = Other.m_pBlockBase;
		m_pBlockEnd = Other.m_pBlockEnd;
		m_pGen2Start = Other.m_pGen2Start;
		m_pGen1Start = Other.m_pGen1Start;
		m_pGen0Start = Other.m_pGen0Start;
		m_pSmallFrontier = Other.m_pSmallFrontier;
		m_pLargeFrontier = Other.m_pLargeFrontier;
		m_nBlockSize = Other.m_nBlockSize;
		m_nSmallAllocationLimit = Other.m_nSmallAllocationLimit;
		m_nLiveAllocationCount = Other.m_nLiveAllocationCount;
		m_nLiveSmallAllocationCount = Other.m_nLiveSmallAllocationCount;
		m_nLiveLargeAllocationCount = Other.m_nLiveLargeAllocationCount;
		m_bIsContiguousLayout = Other.m_bIsContiguousLayout;

		// The moved-from allocator becomes a valid, empty OS-layout allocator.
		Other.m_pBlockAllocation = nullptr;
		Other.m_pBlockBase = nullptr;
		Other.m_pBlockEnd = nullptr;
		Other.m_pGen2Start = nullptr;
		Other.m_pGen1Start = nullptr;
		Other.m_pGen0Start = nullptr;
		Other.m_pSmallFrontier = nullptr;
		Other.m_pLargeFrontier = nullptr;
		Other.m_nBlockSize = 0;
		Other.m_nLiveAllocationCount = 0;
		Other.m_nLiveSmallAllocationCount = 0;
		Other.m_nLiveLargeAllocationCount = 0;
		Other.m_bIsContiguousLayout = false;
	}

	// =========================================================================
	// Address helpers
	// =========================================================================

	Allocator::SizeType Allocator::AlignUpValue(SizeType nValue, SizeType nAlignment)
	{
		return (nValue + nAlignment - 1) & ~(nAlignment - 1);
	}

	Allocator::SizeType Allocator::AlignDownValue(SizeType nValue, SizeType nAlignment)
	{
		return nValue & ~(nAlignment - 1);
	}

	char* Allocator::AlignUpAddress(char* pAddress, SizeType nAlignment)
	{
		uintptr_t uAddress = reinterpret_cast<uintptr_t>(pAddress);
		return reinterpret_cast<char*>(AlignUpValue(static_cast<SizeType>(uAddress), nAlignment));
	}

	bool Allocator::IsPowerOfTwo(SizeType nValue)
	{
		return nValue != 0 && (nValue & (nValue - 1)) == 0;
	}
}