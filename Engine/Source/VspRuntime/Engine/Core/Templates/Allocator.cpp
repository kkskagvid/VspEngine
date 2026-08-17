#include "RuntimePCH.h"

#include "Engine/Core/Templates/Allocator.h"

#include <malloc.h>
#include <utility>

namespace
{
	// Size classes served by SizeClassAllocator, in ascending order.
	constexpr size_t k_nSizeClassTable[Vsp::SizeClassAllocator::k_nSizeClassCount] =
	{
		16, 24, 32, 48, 64, 96, 128, 192, 256, 384, 512, 768, 1024
	};

	// Growth policy for the linear allocator's block chain.
	constexpr size_t k_nMinimumLinearBlockSize = 4096;

	// Growth policy for pool slabs.
	constexpr size_t k_nMinimumSlabSize = 16 * 1024;
	constexpr size_t k_nDefaultSlabSlotCount = 128;
}

namespace Vsp
{
	// =========================================================================
	// PoolAllocator
	// =========================================================================

	PoolAllocator::PoolAllocator() {}

	PoolAllocator::~PoolAllocator()
	{
		ReleaseSlabs();
	}

	PoolAllocator::PoolAllocator(PoolAllocator&& Other)
	{
		MoveFrom(Other);
	}

	PoolAllocator& PoolAllocator::operator=(PoolAllocator&& Other)
	{
		if (this != &Other)
		{
			ReleaseSlabs();
			MoveFrom(Other);
		}
		return *this;
	}

	void PoolAllocator::Initialize(SizeType nSlotPayloadSize)
	{
		m_nSlotPayloadSize = nSlotPayloadSize;
		m_nSlotSize = (AllocatorDetail::k_nAllocationHeaderSize + nSlotPayloadSize + AllocatorDetail::k_nSlotAlignment - 1) &
			~(AllocatorDetail::k_nSlotAlignment - 1);
	}

	void* PoolAllocator::Allocate()
	{
		if (m_nSlotSize == 0)
		{
			DEBUG_BREAK();   // Pool was never initialized.
			return nullptr;
		}

		if (m_pFreeListHead == nullptr)
		{
			GrowSlab();
		}
		if (m_pFreeListHead == nullptr)
		{
			DEBUG_BREAK();
			return nullptr;
		}

		FreeSlot* pSlot = m_pFreeListHead;
		m_pFreeListHead = pSlot->pNextFreeSlot;

		char* pSlotBytes = reinterpret_cast<char*>(pSlot);
		AllocatorDetail::AllocationHeader* pHeader = reinterpret_cast<AllocatorDetail::AllocationHeader*>(pSlotBytes);
		pHeader->uPayloadSize = m_nSlotPayloadSize;
		pHeader->eBackendId = static_cast<uint8_t>(AllocatorDetail::BackendId::SizeClass);
		pHeader->bIsFreed = 0;

		m_nLiveCount++;
		return pSlotBytes + AllocatorDetail::k_nAllocationHeaderSize;
	}

	void PoolAllocator::Free(void* pPayload)
	{
		FreeSlot* pSlot = reinterpret_cast<FreeSlot*>(static_cast<char*>(pPayload) - AllocatorDetail::k_nAllocationHeaderSize);
		pSlot->pNextFreeSlot = m_pFreeListHead;
		m_pFreeListHead = pSlot;
		if (m_nLiveCount > 0)
		{
			m_nLiveCount--;
		}
	}

	void PoolAllocator::Reset()
	{
		ReleaseSlabs();
		m_pFreeListHead = nullptr;
		m_nLiveCount = 0;
	}

	PoolAllocator::SizeType PoolAllocator::GetLiveCount() const
	{
		return m_nLiveCount;
	}

	PoolAllocator::SizeType PoolAllocator::GetSlotPayloadSize() const
	{
		return m_nSlotPayloadSize;
	}

	void PoolAllocator::GrowSlab()
	{
		// One contiguous slab carved into m_nSlotSize slots.
		SizeType nSlotCount = k_nDefaultSlabSlotCount;
		SizeType nSlabSize = sizeof(SlabHeader) + nSlotCount * m_nSlotSize;
		if (nSlabSize < k_nMinimumSlabSize)
		{
			nSlotCount = (k_nMinimumSlabSize - sizeof(SlabHeader) + m_nSlotSize - 1) / m_nSlotSize;
			nSlabSize = sizeof(SlabHeader) + nSlotCount * m_nSlotSize;
		}

		SlabHeader* pSlab = static_cast<SlabHeader*>(_aligned_malloc(nSlabSize, AllocatorDetail::k_nSlotAlignment));
		if (pSlab == nullptr)
		{
			DEBUG_BREAK();
			return;
		}

		pSlab->pNextSlab = m_pFirstSlab;
		pSlab->nSlabSize = nSlabSize;
		m_pFirstSlab = pSlab;

		// Chain every slot into the free list. The link lives inside the slot's
		// header zone, which is free while the slot is unused.
		char* pSlot = reinterpret_cast<char*>(pSlab) + sizeof(SlabHeader);
		char* pSlabEnd = reinterpret_cast<char*>(pSlab) + nSlabSize;
		while (pSlot + m_nSlotSize <= pSlabEnd)
		{
			FreeSlot* pFreeSlot = reinterpret_cast<FreeSlot*>(pSlot);
			pFreeSlot->pNextFreeSlot = m_pFreeListHead;
			m_pFreeListHead = pFreeSlot;
			pSlot += m_nSlotSize;
		}
	}

	void PoolAllocator::ReleaseSlabs()
	{
		SlabHeader* pSlab = m_pFirstSlab;
		while (pSlab != nullptr)
		{
			SlabHeader* pNextSlab = pSlab->pNextSlab;
			_aligned_free(pSlab);
			pSlab = pNextSlab;
		}
		m_pFirstSlab = nullptr;
	}

	void PoolAllocator::MoveFrom(PoolAllocator& Other)
	{
		m_pFreeListHead = Other.m_pFreeListHead;
		m_pFirstSlab = Other.m_pFirstSlab;
		m_nSlotPayloadSize = Other.m_nSlotPayloadSize;
		m_nSlotSize = Other.m_nSlotSize;
		m_nLiveCount = Other.m_nLiveCount;

		Other.m_pFreeListHead = nullptr;
		Other.m_pFirstSlab = nullptr;
		Other.m_nLiveCount = 0;
	}

	// =========================================================================
	// SizeClassAllocator
	// =========================================================================

	SizeClassAllocator::SizeClassAllocator()
	{
		for (SizeType Index = 0; Index < k_nSizeClassCount; ++Index)
		{
			m_Pools[Index].Initialize(k_nSizeClassTable[Index]);
		}
	}

	void* SizeClassAllocator::Allocate(SizeType nSizeInBytes)
	{
		return m_Pools[GetClassIndex(GetSizeClass(nSizeInBytes))].Allocate();
	}

	void SizeClassAllocator::Free(void* pPayload)
	{
		const AllocatorDetail::AllocationHeader* pHeader =
			reinterpret_cast<const AllocatorDetail::AllocationHeader*>(static_cast<const char*>(pPayload) - AllocatorDetail::k_nAllocationHeaderSize);
		m_Pools[GetClassIndex(GetSizeClass(static_cast<SizeType>(pHeader->uPayloadSize)))].Free(pPayload);
	}

	void SizeClassAllocator::Reset()
	{
		for (SizeType Index = 0; Index < k_nSizeClassCount; ++Index)
		{
			m_Pools[Index].Reset();
		}
	}

	SizeClassAllocator::SizeType SizeClassAllocator::GetLiveCount() const
	{
		SizeType nLiveCount = 0;
		for (SizeType Index = 0; Index < k_nSizeClassCount; ++Index)
		{
			nLiveCount += m_Pools[Index].GetLiveCount();
		}
		return nLiveCount;
	}

	SizeClassAllocator::SizeType SizeClassAllocator::GetSizeClass(SizeType nSizeInBytes)
	{
		for (SizeType Index = 0; Index < k_nSizeClassCount; ++Index)
		{
			if (nSizeInBytes <= k_nSizeClassTable[Index])
			{
				return k_nSizeClassTable[Index];
			}
		}
		return k_nSizeClassTable[k_nSizeClassCount - 1];
	}

	SizeClassAllocator::SizeType SizeClassAllocator::GetClassIndex(SizeType nSizeClass)
	{
		for (SizeType Index = 0; Index < k_nSizeClassCount; ++Index)
		{
			if (nSizeClass == k_nSizeClassTable[Index])
			{
				return Index;
			}
		}
		DEBUG_BREAK();
		return k_nSizeClassCount - 1;
	}

	// =========================================================================
	// LinearAllocator
	// =========================================================================

	LinearAllocator::LinearAllocator() {}

	LinearAllocator::~LinearAllocator()
	{
		ReleaseBlocks();
	}

	LinearAllocator::LinearAllocator(LinearAllocator&& Other)
	{
		MoveFrom(Other);
	}

	LinearAllocator& LinearAllocator::operator=(LinearAllocator&& Other)
	{
		if (this != &Other)
		{
			ReleaseBlocks();
			MoveFrom(Other);
		}
		return *this;
	}

	void LinearAllocator::SetInitialBlockSize(SizeType nInitialBlockSize)
	{
		m_nInitialBlockSize = nInitialBlockSize;
	}

	void* LinearAllocator::Allocate(SizeType nSizeInBytes)
	{
		// Slot: header zone (16) + payload + footer (8), rounded up so every
		// slot stays 16-byte aligned.
		SizeType nSlotSize = (AllocatorDetail::k_nAllocationHeaderSize + AllocatorDetail::k_nSlotFooterSize + nSizeInBytes +
			AllocatorDetail::k_nSlotAlignment - 1) & ~(AllocatorDetail::k_nSlotAlignment - 1);

		if (m_pCurrentBlock == nullptr || m_pFrontier + nSlotSize > m_pCurrentBlockEnd)
		{
			// The current block is full: grow a new, larger contiguous block.
			GrowBlock(nSlotSize);
			if (m_pCurrentBlock == nullptr || m_pFrontier + nSlotSize > m_pCurrentBlockEnd)
			{
				DEBUG_BREAK();
				return nullptr;
			}
		}

		char* pSlotStart = m_pFrontier;
		AllocatorDetail::AllocationHeader* pHeader = reinterpret_cast<AllocatorDetail::AllocationHeader*>(pSlotStart);
		pHeader->uPayloadSize = nSizeInBytes;
		pHeader->eBackendId = static_cast<uint8_t>(AllocatorDetail::BackendId::Linear);
		pHeader->bIsFreed = 0;
		*reinterpret_cast<SizeType*>(pSlotStart + nSlotSize - AllocatorDetail::k_nSlotFooterSize) = nSlotSize;

		m_pFrontier = pSlotStart + nSlotSize;
		m_pCurrentBlock->pFrontier = m_pFrontier;
		m_nLiveCount++;
		return pSlotStart + AllocatorDetail::k_nAllocationHeaderSize;
	}

	void LinearAllocator::Free(void* pPayload)
	{
		BlockHeader* pBlock = FindBlock(pPayload);
		if (pBlock == nullptr)
		{
			DEBUG_BREAK();
			return;
		}

		AllocatorDetail::AllocationHeader* pHeader =
			reinterpret_cast<AllocatorDetail::AllocationHeader*>(static_cast<char*>(pPayload) - AllocatorDetail::k_nAllocationHeaderSize);
		if (pHeader->bIsFreed != 0)
		{
			DEBUG_BREAK();   // Double free.
			return;
		}

		pHeader->bIsFreed = 1;
		if (m_nLiveCount > 0)
		{
			m_nLiveCount--;
		}

		// Move the block's frontier back over freed slots at the top; the next
		// allocation reuses and overwrites the reclaimed memory.
		ReclaimBlockFrontier(pBlock);
	}

	void LinearAllocator::Reset()
	{
		ReleaseBlocks();
		m_nLiveCount = 0;
	}

	bool LinearAllocator::ContainsAddress(const void* pAddress) const
	{
		return FindBlock(pAddress) != nullptr;
	}

	LinearAllocator::SizeType LinearAllocator::GetTotalBlockByteCount() const
	{
		SizeType nTotalByteCount = 0;
		for (const BlockHeader* pBlock = m_pFirstBlock; pBlock != nullptr; pBlock = pBlock->pNextBlock)
		{
			nTotalByteCount += pBlock->nBlockSize;
		}
		return nTotalByteCount;
	}

	LinearAllocator::SizeType LinearAllocator::GetLiveCount() const
	{
		return m_nLiveCount;
	}

	void LinearAllocator::GrowBlock(SizeType nRequiredSlotSize)
	{
		SizeType nNewBlockSize = m_nLastBlockSize * 2;
		SizeType nMinimumBlockSize = m_nInitialBlockSize > nRequiredSlotSize ? m_nInitialBlockSize : nRequiredSlotSize;
		if (nMinimumBlockSize < k_nMinimumLinearBlockSize)
		{
			nMinimumBlockSize = k_nMinimumLinearBlockSize;
		}
		if (nNewBlockSize < nMinimumBlockSize)
		{
			nNewBlockSize = nMinimumBlockSize;
		}

		BlockHeader* pBlock = static_cast<BlockHeader*>(_aligned_malloc(sizeof(BlockHeader) + nNewBlockSize, AllocatorDetail::k_nSlotAlignment));
		if (pBlock == nullptr)
		{
			DEBUG_BREAK();
			return;
		}

		pBlock->pNextBlock = m_pFirstBlock;
		pBlock->nBlockSize = nNewBlockSize;
		m_pFirstBlock = pBlock;
		m_pCurrentBlock = pBlock;
		m_pCurrentBlockStart = reinterpret_cast<char*>(pBlock) + sizeof(BlockHeader);
		m_pCurrentBlockEnd = m_pCurrentBlockStart + nNewBlockSize;
		m_pFrontier = m_pCurrentBlockStart;
		pBlock->pFrontier = m_pFrontier;
		m_nLastBlockSize = nNewBlockSize;
	}

	void LinearAllocator::ReclaimBlockFrontier(BlockHeader* pBlock)
	{
		char* pBlockStart = reinterpret_cast<char*>(pBlock) + sizeof(BlockHeader);
		while (pBlock->pFrontier > pBlockStart)
		{
			SizeType nSlotSize = *reinterpret_cast<SizeType*>(pBlock->pFrontier - AllocatorDetail::k_nSlotFooterSize);
			if (nSlotSize < AllocatorDetail::k_nMinimumSlotSize ||
				nSlotSize > static_cast<SizeType>(pBlock->pFrontier - pBlockStart))
			{
				DEBUG_BREAK();
				break;
			}
			const AllocatorDetail::AllocationHeader* pHeader =
				reinterpret_cast<const AllocatorDetail::AllocationHeader*>(pBlock->pFrontier - nSlotSize);
			if (pHeader->bIsFreed == 0)
			{
				break;
			}
			pBlock->pFrontier -= nSlotSize;
		}

		if (pBlock == m_pCurrentBlock)
		{
			m_pFrontier = pBlock->pFrontier;
		}
	}

	LinearAllocator::BlockHeader* LinearAllocator::FindBlock(const void* pPayload) const
	{
		uintptr_t uPayloadAddress = reinterpret_cast<uintptr_t>(pPayload);
		for (BlockHeader* pBlock = m_pFirstBlock; pBlock != nullptr; pBlock = pBlock->pNextBlock)
		{
			uintptr_t uBlockStart = reinterpret_cast<uintptr_t>(pBlock) + sizeof(BlockHeader);
			uintptr_t uBlockEnd = uBlockStart + pBlock->nBlockSize;
			if (uPayloadAddress >= uBlockStart && uPayloadAddress < uBlockEnd)
			{
				return pBlock;
			}
		}
		return nullptr;
	}

	void LinearAllocator::ReleaseBlocks()
	{
		BlockHeader* pBlock = m_pFirstBlock;
		while (pBlock != nullptr)
		{
			BlockHeader* pNextBlock = pBlock->pNextBlock;
			_aligned_free(pBlock);
			pBlock = pNextBlock;
		}
		m_pFirstBlock = nullptr;
		m_pCurrentBlock = nullptr;
		m_pCurrentBlockStart = nullptr;
		m_pCurrentBlockEnd = nullptr;
		m_pFrontier = nullptr;
		m_nLastBlockSize = 0;
	}

	void LinearAllocator::MoveFrom(LinearAllocator& Other)
	{
		m_pFirstBlock = Other.m_pFirstBlock;
		m_pCurrentBlock = Other.m_pCurrentBlock;
		m_pCurrentBlockStart = Other.m_pCurrentBlockStart;
		m_pCurrentBlockEnd = Other.m_pCurrentBlockEnd;
		m_pFrontier = Other.m_pFrontier;
		m_nInitialBlockSize = Other.m_nInitialBlockSize;
		m_nLastBlockSize = Other.m_nLastBlockSize;
		m_nLiveCount = Other.m_nLiveCount;

		Other.m_pFirstBlock = nullptr;
		Other.m_pCurrentBlock = nullptr;
		Other.m_pCurrentBlockStart = nullptr;
		Other.m_pCurrentBlockEnd = nullptr;
		Other.m_pFrontier = nullptr;
		Other.m_nLastBlockSize = 0;
		Other.m_nLiveCount = 0;
	}

	// =========================================================================
	// SystemAllocator
	// =========================================================================

	void* SystemAllocator::Allocate(SizeType nSizeInBytes)
	{
		SizeType nTotalByteCount = AllocatorDetail::k_nAllocationHeaderSize + nSizeInBytes;
		char* pBase = static_cast<char*>(_aligned_malloc(nTotalByteCount, AllocatorDetail::k_nAllocationHeaderSize));
		if (pBase == nullptr)
		{
			DEBUG_BREAK();
			return nullptr;
		}

		AllocatorDetail::AllocationHeader* pHeader = reinterpret_cast<AllocatorDetail::AllocationHeader*>(pBase);
		pHeader->uPayloadSize = nSizeInBytes;
		pHeader->eBackendId = static_cast<uint8_t>(AllocatorDetail::BackendId::System);
		pHeader->bIsFreed = 0;

		m_nLiveCount++;
		return pBase + AllocatorDetail::k_nAllocationHeaderSize;
	}

	void SystemAllocator::Free(void* pPayload)
	{
		_aligned_free(static_cast<char*>(pPayload) - AllocatorDetail::k_nAllocationHeaderSize);
		if (m_nLiveCount > 0)
		{
			m_nLiveCount--;
		}
	}

	SystemAllocator::SizeType SystemAllocator::GetLiveCount() const
	{
		return m_nLiveCount;
	}

	// =========================================================================
	// Allocator
	// =========================================================================

	Allocator::Allocator(SizeType nSizeClassLimit, SizeType nLinearLimit)
		: m_nSizeClassLimit(nSizeClassLimit)
		, m_nLinearLimit(nLinearLimit)
	{
	}

	Allocator Allocator::CreateContiguous(SizeType nContiguousBlockSize)
	{
		Allocator ContiguousAllocator;
		ContiguousAllocator.m_bIsContiguousLayoutMode = true;
		ContiguousAllocator.m_LinearAllocator.SetInitialBlockSize(nContiguousBlockSize);
		return ContiguousAllocator;
	}

	Allocator::Allocator(Allocator&& Other)
	{
		m_SizeClassAllocator = std::move(Other.m_SizeClassAllocator);
		m_LinearAllocator = std::move(Other.m_LinearAllocator);
		MoveFrom(Other);
	}

	Allocator& Allocator::operator=(Allocator&& Other)
	{
		if (this != &Other)
		{
			m_SizeClassAllocator = std::move(Other.m_SizeClassAllocator);
			m_LinearAllocator = std::move(Other.m_LinearAllocator);
			MoveFrom(Other);
		}
		return *this;
	}

	Allocator::~Allocator() {}

	void* Allocator::Allocate(SizeType nSizeInBytes)
	{
		if (nSizeInBytes == 0)
		{
			DEBUG_BREAK();
			nSizeInBytes = 1;
		}

		// Contiguous mode: the linear block chain serves everything.
		if (m_bIsContiguousLayoutMode)
		{
			return m_LinearAllocator.Allocate(nSizeInBytes);
		}

		// Hybrid mode: pick the backend that fits the request best.
		if (nSizeInBytes <= m_nSizeClassLimit)
		{
			return m_SizeClassAllocator.Allocate(nSizeInBytes);
		}
		if (nSizeInBytes <= m_nLinearLimit)
		{
			return m_LinearAllocator.Allocate(nSizeInBytes);
		}
		return m_SystemAllocator.Allocate(nSizeInBytes);
	}

	void* Allocator::AllocateAligned(SizeType nSizeInBytes, SizeType nAlignment)
	{
		if (nAlignment == 0 || nAlignment > k_nMaximumAlignment || !IsPowerOfTwo(nAlignment))
		{
			// Unsupported alignment: clamp to the maximum the allocator can give.
			DEBUG_BREAK();
			nAlignment = k_nMaximumAlignment;
		}

		// Every backend hands out 16-byte aligned payloads, which satisfies any
		// alignment up to k_nMaximumAlignment.
		return Allocate(nSizeInBytes);
	}

	void Allocator::Free(void* pMemory)
	{
		if (pMemory == nullptr)
		{
			DEBUG_BREAK();
			return;
		}

		// The header in front of the payload names the owning backend, so the
		// free path is a single O(1) dispatch.
		const AllocatorDetail::AllocationHeader* pHeader = reinterpret_cast<const AllocatorDetail::AllocationHeader*>(
			static_cast<const char*>(pMemory) - AllocatorDetail::k_nAllocationHeaderSize);
		switch (static_cast<AllocatorDetail::BackendId>(pHeader->eBackendId))
		{
		case AllocatorDetail::BackendId::SizeClass:
			m_SizeClassAllocator.Free(pMemory);
			break;
		case AllocatorDetail::BackendId::Linear:
			m_LinearAllocator.Free(pMemory);
			break;
		case AllocatorDetail::BackendId::System:
			m_SystemAllocator.Free(pMemory);
			break;
		default:
			DEBUG_BREAK();
			break;
		}
	}

	void Allocator::Reset()
	{
		m_SizeClassAllocator.Reset();
		m_LinearAllocator.Reset();
	}

	bool Allocator::IsContiguousLayoutMode() const
	{
		return m_bIsContiguousLayoutMode;
	}

	Allocator::SizeType Allocator::GetBlockSize() const
	{
		return m_LinearAllocator.GetTotalBlockByteCount();
	}

	Allocator::SizeType Allocator::GetSizeClassLimit() const
	{
		return m_nSizeClassLimit;
	}

	Allocator::SizeType Allocator::GetLinearLimit() const
	{
		return m_nLinearLimit;
	}

	bool Allocator::ContainsAddress(const void* pAddress) const
	{
		return m_LinearAllocator.ContainsAddress(pAddress);
	}

	Allocator::SizeType Allocator::GetLiveAllocationCount() const
	{
		return m_SizeClassAllocator.GetLiveCount() + m_LinearAllocator.GetLiveCount() + m_SystemAllocator.GetLiveCount();
	}

	Allocator::SizeType Allocator::GetLivePoolAllocationCount() const
	{
		return m_SizeClassAllocator.GetLiveCount();
	}

	Allocator::SizeType Allocator::GetLiveLinearAllocationCount() const
	{
		return m_LinearAllocator.GetLiveCount();
	}

	Allocator::SizeType Allocator::GetLiveSystemAllocationCount() const
	{
		return m_SystemAllocator.GetLiveCount();
	}

	bool Allocator::IsEmpty() const
	{
		return GetLiveAllocationCount() == 0;
	}

	void Allocator::MoveFrom(Allocator& Other)
	{
		m_nSizeClassLimit = Other.m_nSizeClassLimit;
		m_nLinearLimit = Other.m_nLinearLimit;
		m_bIsContiguousLayoutMode = Other.m_bIsContiguousLayoutMode;

		Other.m_nSizeClassLimit = k_nDefaultSizeClassLimit;
		Other.m_nLinearLimit = k_nDefaultLinearLimit;
		Other.m_bIsContiguousLayoutMode = false;
	}

	bool Allocator::IsPowerOfTwo(SizeType nValue)
	{
		return nValue != 0 && (nValue & (nValue - 1)) == 0;
	}
}
