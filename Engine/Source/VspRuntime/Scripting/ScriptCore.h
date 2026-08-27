#pragma once

#include "Core/Core.h"
#include "Core/Templates/ArrayList.h"

namespace Vsp
{
	// -------------------------------------------------------------------------
	// ScriptCore
	// -------------------------------------------------------------------------
	// Central point where all state exchanged with managed scripts lives:
	//   - frame timing (queried by managed Time.DeltaTime / Time.ElapsedTime)
	//   - per-script-instance transforms (keyed by non-negative InstanceID)
	//   - renderer commands issued from scripts (triangle color mode)
	// The native exports (NativeExports.cpp) forward C# calls into this class,
	// and the game loop reads it back to drive the renderer.
	// -------------------------------------------------------------------------
#pragma warning(push)
#pragma warning(disable : 4251)   // ArrayList member: header-only template.
	class RUNTIME_API ScriptCore
	{
	public:
		struct TransformEntry
		{
			uint32_t uInstanceId = 0;
			float fPositionX = 0.0f;
			float fPositionY = 0.0f;
		};

		static constexpr int32_t k_nDefaultColorMode = 3;   // MultiColor

		static ScriptCore& Get();

		// -------- Time --------
		void SetDeltaTime(float fDeltaSeconds) { m_fDeltaTime = fDeltaSeconds; }
		float GetDeltaTime() const { return m_fDeltaTime; }

		void SetElapsedTime(float fElapsedSeconds) { m_fElapsedTime = fElapsedSeconds; }
		float GetElapsedTime() const { return m_fElapsedTime; }

		// -------- Transforms --------
		void SetTransformPosition(uint32_t uInstanceId, float fPositionX, float fPositionY);
		bool GetTransformPosition(uint32_t uInstanceId, float& outPositionX, float& outPositionY) const;

		// -------- Renderer commands --------
		void SetColorMode(uint32_t uInstanceId, int32_t nColorMode);
		int32_t GetColorMode(uint32_t uInstanceId) const;

	private:
		ScriptCore() = default;

		TransformEntry* FindTransformEntry(uint32_t uInstanceId);
		const TransformEntry* FindTransformEntry(uint32_t uInstanceId) const;

		ArrayList<TransformEntry> m_TransformEntries;
		float m_fDeltaTime = 0.0f;
		float m_fElapsedTime = 0.0f;
		int32_t m_nColorMode = k_nDefaultColorMode;
	};
#pragma warning(pop)
}
