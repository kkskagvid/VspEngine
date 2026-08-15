#pragma once

#include <cstddef>
#include <cstring>
#include <memory>
#include <type_traits>
#include <utility>

#include "Engine/Core/Core.h"
#include "Engine/Core/Templates/Allocator.h"

namespace Vsp
{
	// Dynamically resized array that stores its elements in one contiguous block of memory.
	// Capacity grows geometrically (x1.5), so appending amortizes to constant time.
	// No bounds checking is done by operator[]; At() checks the index range instead.
	//
	// All storage comes from the array's private Allocator. By default it is
	// OS-layout (unbounded growth, stable element pointers); constructing the
	// array with a block size switches it to the contiguous layout, where every
	// buffer is carved out of that one reserved block. When the contiguous
	// block is exhausted, the array migrates to a fresh, larger contiguous
	// block (doubling each time); the migration fails only if reserving a new
	// block itself fails, in which case DEBUG_BREAK() fires and the existing
	// elements are kept.
	template <typename Type>
	class ArrayList
	{
	public:
		using SizeType = size_t;

		// -------- Construction / destruction --------
		ArrayList() {}

		// Constructs an empty array, provided for convenient reset-style initialization.
		ArrayList(std::nullptr_t) {}

		// Contiguous-layout construction: reserves one block of
		// nContiguousBlockSize bytes and serves every buffer from inside it.
		// The buffers are placed in the allocator's large-object area, which is
		// never compacted, so element pointers stay stable between migrations.
		// When the block is exhausted the array migrates to a fresh, larger
		// contiguous block (see the class comment).
		explicit ArrayList(SizeType nContiguousBlockSize)
			: m_Allocator(nContiguousBlockSize, 0)
		{
		}

		ArrayList(const ArrayList& Other)
			: m_Allocator(Other.m_Allocator.GetBlockSize(), Other.m_Allocator.GetSmallAllocationLimit())
		{
			Reserve(Other.m_Size);
			for (SizeType Index = 0; Index < Other.m_Size; ++Index)
			{
				std::construct_at(m_pData + Index, Other.m_pData[Index]);
			}
			m_Size = Other.m_Size;
		}

		ArrayList(ArrayList&& Other)
			: m_pData(Other.m_pData)
			, m_Capacity(Other.m_Capacity)
			, m_Size(Other.m_Size)
			, m_Allocator(std::move(Other.m_Allocator))
		{
			Other.m_pData = nullptr;
			Other.m_Capacity = 0;
			Other.m_Size = 0;
		}

		~ArrayList()
		{
			Clear();
			FreeData();
		}

		// -------- Assignment --------
		ArrayList& operator=(const ArrayList& Other)
		{
			if (this != &Other)
			{
				Clear();
				Reserve(Other.m_Size);
				for (SizeType Index = 0; Index < Other.m_Size; ++Index)
				{
					std::construct_at(m_pData + Index, Other.m_pData[Index]);
				}
				m_Size = Other.m_Size;
			}
			return *this;
		}

		ArrayList& operator=(ArrayList&& Other)
		{
			if (this != &Other)
			{
				Clear();
				FreeData();
				m_pData = Other.m_pData;
				m_Capacity = Other.m_Capacity;
				m_Size = Other.m_Size;
				m_Allocator = std::move(Other.m_Allocator);
				Other.m_pData = nullptr;
				Other.m_Capacity = 0;
				Other.m_Size = 0;
			}
			return *this;
		}

		// Resets the array to an empty state, releasing all allocated memory.
		ArrayList& operator=(std::nullptr_t)
		{
			Clear();
			FreeData();
			return *this;
		}

		// -------- Add --------
		// Appends a copy of Value to the end of the array and returns it. When
		// the array cannot grow (migration to a new block failed),
		// DEBUG_BREAK() fires and the returned reference falls back to the
		// first element; the array itself is left unchanged.
		Type& Add(const Type& Value)
		{
			if (!EnsureCapacityForAdd())
			{
				DEBUG_BREAK();
				return m_pData[0];
			}
			Type* pElement = std::construct_at(m_pData + m_Size, Value);
			++m_Size;
			return *pElement;
		}

		// Appends Value to the end of the array, moving it, and returns it.
		Type& Add(Type&& Value)
		{
			if (!EnsureCapacityForAdd())
			{
				DEBUG_BREAK();
				return m_pData[0];
			}
			Type* pElement = std::construct_at(m_pData + m_Size, std::move(Value));
			++m_Size;
			return *pElement;
		}

		// Constructs a new element in place at the end of the array from Arguments and returns it.
		template <typename... ArgumentTypes>
		Type& Emplace(ArgumentTypes&&... Arguments)
		{
			if (!EnsureCapacityForAdd())
			{
				DEBUG_BREAK();
				return m_pData[0];
			}
			Type* pElement = std::construct_at(m_pData + m_Size, std::forward<ArgumentTypes>(Arguments)...);
			++m_Size;
			return *pElement;
		}

		// -------- Remove --------
		// Removes the element at Index. All following elements are shifted down to keep the array contiguous.
		void RemoveAt(SizeType Index)
		{
			if (Index >= m_Size)
			{
				DEBUG_BREAK();
				return;
			}

			SizeType ElementCountToMove = m_Size - Index - 1;
			if constexpr (std::is_trivially_copyable_v<Type>)
			{
				// Trivially copyable elements can be shifted byte-wise, no destruction required.
				if (ElementCountToMove > 0)
				{
					memmove(m_pData + Index, m_pData + Index + 1, ElementCountToMove * sizeof(Type));
				}
			}
			else
			{
				for (SizeType MoveIndex = Index; MoveIndex < m_Size - 1; ++MoveIndex)
				{
					m_pData[MoveIndex] = std::move(m_pData[MoveIndex + 1]);
				}
				std::destroy_at(m_pData + m_Size - 1);
			}
			--m_Size;
		}

		// Removes the last element. Calling it on an empty array does nothing.
		void PopBack()
		{
			if (m_Size == 0)
			{
				DEBUG_BREAK();
				return;
			}
			--m_Size;
			std::destroy_at(m_pData + m_Size);
		}

		// Destroys all elements and makes the array empty. Allocated capacity is kept for reuse.
		void Clear()
		{
			if constexpr (!std::is_trivially_destructible_v<Type>)
			{
				for (SizeType Index = 0; Index < m_Size; ++Index)
				{
					std::destroy_at(m_pData + Index);
				}
			}
			m_Size = 0;
		}

		// -------- Capacity --------
		// Ensures the array can hold at least MinCapacity elements without reallocating.
		void Reserve(SizeType MinCapacity)
		{
			if (MinCapacity > m_Capacity)
			{
				GrowToCapacity(MinCapacity);
			}
		}

		SizeType GetSize() const { return m_Size; }

		SizeType GetCapacity() const { return m_Capacity; }

		// Number of bytes reserved by the underlying allocator: 0 when it is
		// OS-layout (the default), the current block size when the array was
		// constructed with the contiguous layout. The value grows as the array
		// migrates to larger blocks.
		SizeType GetAllocatorBlockSize() const { return m_Allocator.GetBlockSize(); }

		bool IsEmpty() const { return m_Size == 0; }

		Type* GetData() { return m_pData; }

		const Type* GetData() const { return m_pData; }

		// -------- Element access --------
		// Unchecked access. The index must be within [0, Size).
		Type& operator[](SizeType Index) { return m_pData[Index]; }

		const Type& operator[](SizeType Index) const { return m_pData[Index]; }

		// Range-checked access: out-of-range access triggers DEBUG_BREAK() in debug builds and the
		// index is clamped to the last valid element. Calling At() on an empty array is invalid usage.
		Type& At(SizeType Index) { return m_pData[ClampCheckedIndex(Index)]; }

		const Type& At(SizeType Index) const { return m_pData[ClampCheckedIndex(Index)]; }

	private:
		// -------- Growth --------
		// Grows when needed and reports whether at least one more element fits.
		bool EnsureCapacityForAdd()
		{
			if (m_Size >= m_Capacity)
			{
				GrowToCapacity(CalculateGrowCapacity(m_Size + 1));
			}
			return m_Size < m_Capacity;
		}

		// Geometric growth (x1.5, plus a small constant so tiny arrays grow to a usable size
		// right away) keeps appends amortized O(1) while limiting wasted memory.
		SizeType CalculateGrowCapacity(SizeType RequiredCapacity)
		{
			SizeType GrownCapacity = m_Capacity + (m_Capacity >> 1) + 8;
			return GrownCapacity > RequiredCapacity ? GrownCapacity : RequiredCapacity;
		}

		void GrowToCapacity(SizeType NewCapacity)
		{
			// The allocator owns the buffer and offers no in-place realloc, so a
			// growing array always relocates: allocate a fresh block, move the
			// elements over, then hand the old block back to the allocator.
			SizeType nRequiredByteCount = NewCapacity * sizeof(Type);
			Type* pNewData = static_cast<Type*>(m_Allocator.Allocate(nRequiredByteCount));
			Allocator NewAllocator;   // Only used when migrating to a new block.
			bool bMigratedToNewBlock = false;
			if (pNewData == nullptr)
			{
				// The contiguous block is exhausted: migrate to a fresh, larger
				// contiguous block. OS layout cannot run out of blocks, so this
				// only ever happens in contiguous mode.
				if (!TryCreateLargerContiguousBlock(nRequiredByteCount, NewAllocator, pNewData))
				{
					DEBUG_BREAK();
					return;
				}
				bMigratedToNewBlock = true;
			}

			RelocateElementsTo(pNewData);

			if (bMigratedToNewBlock)
			{
				// Adopt the new block. Releasing the old allocator frees the old
				// block wholesale, so no per-buffer Free() is needed.
				m_Allocator = std::move(NewAllocator);
			}
			else if (m_pData != nullptr)
			{
				m_Allocator.Free(m_pData);
			}
			m_pData = pNewData;
			m_Capacity = NewCapacity;
		}

		// Moves the live elements from m_pData to pNewData, destroying the
		// originals for non-trivial types.
		void RelocateElementsTo(Type* pNewData)
		{
			if constexpr (std::is_trivially_copyable_v<Type>)
			{
				// Trivially copyable elements relocate byte-wise; no destruction required.
				if (m_Size > 0)
				{
					memcpy(pNewData, m_pData, m_Size * sizeof(Type));
				}
			}
			else
			{
				for (SizeType Index = 0; Index < m_Size; ++Index)
				{
					std::construct_at(pNewData + Index, std::move(m_pData[Index]));
					std::destroy_at(m_pData + Index);
				}
			}
		}

		// Reserves a fresh contiguous block - at least twice the current one
		// and at least twice the requested bytes - and allocates OutNewData
		// from it. Returns false when migration is impossible (OS layout) or
		// reserving the new block itself failed.
		bool TryCreateLargerContiguousBlock(SizeType nRequiredByteCount, Allocator& OutNewAllocator, Type*& OutNewData)
		{
			SizeType nCurrentBlockSize = m_Allocator.GetBlockSize();
			if (nCurrentBlockSize == 0)
			{
				return false;   // OS layout cannot run out of blocks.
			}

			SizeType nNewBlockSize = nCurrentBlockSize * 2;
			SizeType nMinimumBlockSize = nRequiredByteCount * 2;
			if (nNewBlockSize < nMinimumBlockSize)
			{
				nNewBlockSize = nMinimumBlockSize;
			}

			Allocator NewAllocator(nNewBlockSize, 0);
			if (!NewAllocator.IsContiguousLayoutMode())
			{
				DEBUG_BREAK();   // Reserving the new block itself failed.
				return false;
			}

			OutNewData = static_cast<Type*>(NewAllocator.Allocate(nRequiredByteCount));
			if (OutNewData == nullptr)
			{
				DEBUG_BREAK();
				return false;
			}
			OutNewAllocator = std::move(NewAllocator);
			return true;
		}

		void FreeData()
		{
			if (m_pData != nullptr)
			{
				m_Allocator.Free(m_pData);
				m_pData = nullptr;
			}
			m_Capacity = 0;
		}

		SizeType ClampCheckedIndex(SizeType Index) const
		{
			if (Index >= m_Size)
			{
				DEBUG_BREAK();
				Index = m_Size > 0 ? m_Size - 1 : 0;
			}
			return Index;
		}

	private:
		// Every storage block is requested from and returned to this allocator.
		// Default construction uses the OS layout; constructing with a block
		// size switches to the contiguous layout (see the constructors).
		Allocator m_Allocator;

		Type* m_pData = nullptr;
		SizeType m_Size = 0;
		SizeType m_Capacity = 0;
	};
}
