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
	// hybrid (small buffers from size-class pools, medium buffers from the
	// linear block chain, huge buffers from the OS); constructing the array
	// with a block size selects the contiguous mode, where every buffer comes
	// out of the allocator's linear block chain. When a block is exhausted the
	// allocator grows a new, larger contiguous block and the buffers are never
	// copied; growth only fails if the OS itself runs out of memory, in which
	// case DEBUG_BREAK() fires and the existing elements are kept.
	template <typename Type>
	class ArrayList
	{
	public:
		using SizeType = size_t;

		// -------- Construction / destruction --------
		ArrayList() {}

		// Constructs an empty array, provided for convenient reset-style initialization.
		ArrayList(std::nullptr_t) {}

		// Contiguous-mode construction: every buffer is carved out of the
		// allocator's linear block chain, starting with a block of
		// nContiguousBlockSize bytes (the chain grows beyond it on demand).
		explicit ArrayList(SizeType nContiguousBlockSize)
			: m_Allocator(Allocator::CreateContiguous(nContiguousBlockSize))
		{
		}

		ArrayList(const ArrayList& Other)
		{
			// Mirror the source's allocator layout: contiguous arrays keep the
			// contiguous mode, hybrid arrays copy the same size limits.
			if (Other.m_Allocator.IsContiguousLayoutMode())
			{
				m_Allocator = Allocator::CreateContiguous(Other.m_Allocator.GetBlockSize());
			}
			else
			{
				m_Allocator = Allocator(Other.m_Allocator.GetSizeClassLimit(), Other.m_Allocator.GetLinearLimit());
			}

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
			// growing array always relocates: allocate a fresh buffer, move the
			// elements over, then hand the old buffer back to the allocator.
			// In contiguous mode the allocator itself grows new contiguous
			// blocks when the current one is full, so growth never stops (until
			// the OS runs out of memory) and buffers are never copied for the
			// block chain.
			Type* pNewData = static_cast<Type*>(m_Allocator.Allocate(NewCapacity * sizeof(Type)));
			if (pNewData == nullptr)
			{
				DEBUG_BREAK();
				return;
			}

			RelocateElementsTo(pNewData);

			if (m_pData != nullptr)
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
		// Every storage buffer is requested from and returned to this
		// allocator. Default construction uses the hybrid mode (size-based
		// routing); constructing with a block size switches to the contiguous
		// mode (see the constructors).
		Allocator m_Allocator;

		Type* m_pData = nullptr;
		SizeType m_Size = 0;
		SizeType m_Capacity = 0;
	};
}
