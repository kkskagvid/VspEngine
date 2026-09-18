#pragma once

#include "Core/Core.h"
#include "Core/String/VspString.h"

namespace Vsp
{
	// -------------------------------------------------------------------------
	// NativeObjectHandle
	// -------------------------------------------------------------------------
	// One 32-bit value addressing any native engine object:
	//
	//   bits 28..31  object kind   (GameObject / Transform / Component)
	//   bits 20..27  slot generation (bumped every time a slot is recycled)
	//   bits  0..19  slot index    (1-based, so a handle is never 0)
	//
	// The kind tag makes one handle space serve every object kind and the
	// generation makes recycled slots safe: a stale handle simply stops
	// resolving instead of addressing whichever object took the slot over.
	// 0 always means "invalid".
	//
	// The managed engine classes (VspEngine.Object and everything derived from
	// it) store nothing but this handle: they are reference handles whose real
	// data lives in the native scene (Classes/Scene).
	// -------------------------------------------------------------------------
	using NativeObjectHandle = uint32;

	static constexpr NativeObjectHandle k_nInvalidObjectHandle = 0;

	static constexpr uint32 k_nObjectHandleIndexBitCount = 20;
	static constexpr uint32 k_nObjectHandleIndexMask = (1u << k_nObjectHandleIndexBitCount) - 1u;
	static constexpr uint32 k_nObjectHandleIndexBitOffset = 0;

	static constexpr uint32 k_nObjectHandleGenerationBitCount = 8;
	static constexpr uint32 k_nObjectHandleGenerationMask = (1u << k_nObjectHandleGenerationBitCount) - 1u;
	static constexpr uint32 k_nObjectHandleGenerationBitOffset = k_nObjectHandleIndexBitCount;

	static constexpr uint32 k_nObjectHandleKindBitOffset =
		k_nObjectHandleIndexBitCount + k_nObjectHandleGenerationBitCount;

	enum class NativeObjectKind : uint32
	{
		None = 0,
		GameObject = 1,
		Transform = 2,
		Component = 3,
		Shader = 4,
		Material = 5,
	};

	// Builds the handle of the object stored in slot uSlotIndex of its kind.
	INLINE NativeObjectHandle MakeObjectHandle(NativeObjectKind eKind, uint32 uGeneration, uint32 uSlotIndex)
	{
		return (static_cast<uint32>(eKind) << k_nObjectHandleKindBitOffset) |
			((uGeneration & k_nObjectHandleGenerationMask) << k_nObjectHandleGenerationBitOffset) |
			(((uSlotIndex + 1u) & k_nObjectHandleIndexMask) << k_nObjectHandleIndexBitOffset);
	}

	INLINE NativeObjectKind GetObjectHandleKind(NativeObjectHandle uHandle)
	{
		return static_cast<NativeObjectKind>(uHandle >> k_nObjectHandleKindBitOffset);
	}

	INLINE uint32 GetObjectHandleGeneration(NativeObjectHandle uHandle)
	{
		return (uHandle >> k_nObjectHandleGenerationBitOffset) & k_nObjectHandleGenerationMask;
	}

	// Slot index carried by the handle; only meaningful for a non-zero handle.
	INLINE uint32 GetObjectHandleSlotIndex(NativeObjectHandle uHandle)
	{
		return ((uHandle >> k_nObjectHandleIndexBitOffset) & k_nObjectHandleIndexMask) - 1u;
	}

	// -------------------------------------------------------------------------
	// NativeObject
	// -------------------------------------------------------------------------
	// Data every native engine object carries: the handle that addresses it,
	// its kind and the name shown in tools and logs. GameObject, Transform and
	// Component all inherit from it, so one lookup path serves all of them.
	//
	// Lifetime: the Scene places every object in a slot of its kind's table and
	// binds it. Releasing the object unbinds it and bumps the stored generation,
	// so any handle still pointing at the old occupant stops resolving.
	// -------------------------------------------------------------------------
	class RUNTIME_API NativeObject
	{
	public:
		// Binds the object to the slot it was placed in; called by the Scene.
		void BindToSlot(NativeObjectKind eKind, uint32 uSlotIndex);

		// Detaches the object from its slot and ages the generation so stale
		// handles no longer resolve; called by the Scene on destruction.
		void UnbindFromSlot();

		NativeObjectHandle GetHandle() const { return m_uHandle; }
		NativeObjectKind GetKind() const { return m_eKind; }
		bool IsValid() const { return m_uHandle != k_nInvalidObjectHandle; }

		// Generation the next BindToSlot will publish for this slot.
		uint32 GetSlotGeneration() const { return m_uSlotGeneration; }

		const VspString& GetName() const { return m_sName; }
		void SetName(const VspString& sName);

	private:
		NativeObjectHandle m_uHandle = k_nInvalidObjectHandle;
		NativeObjectKind m_eKind = NativeObjectKind::None;
		uint32 m_uSlotGeneration = 0;
		VspString m_sName;
	};
}
