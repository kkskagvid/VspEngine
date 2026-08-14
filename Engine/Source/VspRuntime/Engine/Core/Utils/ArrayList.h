#pragma once

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <type_traits>
#include <utility>

#include "Engine/Core/Core.h"

namespace Vsp
{
	// Dynamically resized array that stores its elements in one contiguous block of memory.
	// Capacity grows geometrically (x1.5), so appending amortizes to constant time.
	// No bounds checking is done by operator[]; At() checks the index range instead.
	template <typename Type>
	class ArrayList
	{
	public:
		using SizeType = size_t;

		// -------- Construction / destruction --------
		ArrayList() {}

		// Constructs an empty array, provided for convenient reset-style initialization.
		ArrayList(std::nullptr_t) {}

		ArrayList(const ArrayList& Other)
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
		// Appends a copy of Value to the end of the array and returns it.
		Type& Add(const Type& Value)
		{
			EnsureCapacityForAdd();
			Type* pElement = std::construct_at(m_pData + m_Size, Value);
			++m_Size;
			return *pElement;
		}

		// Appends Value to the end of the array, moving it, and returns it.
		Type& Add(Type&& Value)
		{
			EnsureCapacityForAdd();
			Type* pElement = std::construct_at(m_pData + m_Size, std::move(Value));
			++m_Size;
			return *pElement;
		}

		// Constructs a new element in place at the end of the array from Arguments and returns it.
		template <typename... ArgumentTypes>
		Type& Emplace(ArgumentTypes&&... Arguments)
		{
			EnsureCapacityForAdd();
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
		void EnsureCapacityForAdd()
		{
			if (m_Size >= m_Capacity)
			{
				GrowToCapacity(CalculateGrowCapacity(m_Size + 1));
			}
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
			if constexpr (std::is_trivially_copyable_v<Type>)
			{
				// Trivially copyable elements are relocated byte-wise; realloc extends the block
				// in place whenever possible, skipping the copy entirely.
				Type* pNewData = static_cast<Type*>(realloc(m_pData, NewCapacity * sizeof(Type)));
				if (pNewData == nullptr)
				{
					DEBUG_BREAK();
					return;
				}
				m_pData = pNewData;
			}
			else
			{
				Type* pNewData = static_cast<Type*>(malloc(NewCapacity * sizeof(Type)));
				if (pNewData == nullptr)
				{
					DEBUG_BREAK();
					return;
				}
				for (SizeType Index = 0; Index < m_Size; ++Index)
				{
					std::construct_at(pNewData + Index, std::move(m_pData[Index]));
					std::destroy_at(m_pData + Index);
				}
				free(m_pData);
				m_pData = pNewData;
			}
			m_Capacity = NewCapacity;
		}

		void FreeData()
		{
			free(m_pData);
			m_pData = nullptr;
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
		Type* m_pData = nullptr;
		SizeType m_Size = 0;
		SizeType m_Capacity = 0;
	};
}
