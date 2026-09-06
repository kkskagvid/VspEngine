#pragma once

#include "Core/Core.h"
#include "Core/String/VspStringFormat.h"
#include "Core/Templates/ArrayList.h"

namespace Vsp
{
	// -------------------------------------------------------------------------
	// Position2D
	// -------------------------------------------------------------------------
	// Plain 2D position pair. Demonstrates custom VspFormat support: the
	// VspFormatter specialization below teaches VspFormat how to print it with
	// the ":p" specifier ("(x, y)").
	// -------------------------------------------------------------------------
	struct Position2D
	{
		float fPositionX = 0.0f;
		float fPositionY = 0.0f;
	};

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
			uint32 uInstanceId = 0;
			float fPositionX = 0.0f;
			float fPositionY = 0.0f;
		};

		static constexpr int32 k_nDefaultColorMode = 3;   // MultiColor

		static ScriptCore& Get();

		// -------- Time --------
		void SetDeltaTime(float fDeltaSeconds) { m_fDeltaTime = fDeltaSeconds; }
		float GetDeltaTime() const { return m_fDeltaTime; }

		void SetElapsedTime(float fElapsedSeconds) { m_fElapsedTime = fElapsedSeconds; }
		float GetElapsedTime() const { return m_fElapsedTime; }

		// -------- Transforms --------
		void SetTransformPosition(uint32 uInstanceId, float fPositionX, float fPositionY);
		bool GetTransformPosition(uint32 uInstanceId, float& outPositionX, float& outPositionY) const;

		// -------- Renderer commands --------
		void SetColorMode(uint32 uInstanceId, int32 nColorMode);
		int32 GetColorMode(uint32 uInstanceId) const;

	private:
		ScriptCore() = default;

		TransformEntry* FindTransformEntry(uint32 uInstanceId);
		const TransformEntry* FindTransformEntry(uint32 uInstanceId) const;

		ArrayList<TransformEntry> m_TransformEntries;
		float m_fDeltaTime = 0.0f;
		float m_fElapsedTime = 0.0f;
		int32 m_nColorMode = k_nDefaultColorMode;
	};
#pragma warning(pop)
}

namespace Vsp
{
	// Custom VspFormat support for Position2D: the ":p" specifier prints the
	// pair as "(x, y)"; any other (or missing) specifier falls back to the
	// same representation.
	template <>
	struct VspFormatter<Position2D>
	{
		// ":p" selects the "(x, y)" form; ":P" selects the compact "x, y" form.
		// Anything else falls back to "(x, y)". The decision made here is
		// stored in the context and consumed by Format.
		static void Parse(const char* pSpecBegin, const char* pSpecEnd, VspFormatContext& context)
		{
			context.eSpec = (pSpecBegin != nullptr && pSpecEnd != nullptr &&
				pSpecEnd - pSpecBegin == 1 && *pSpecBegin == 'P')
				? VspFormatSpec::FixedFloat
				: VspFormatSpec::Default;
		}

		static void Format(const VspFormatContext& context, VspString& out, const Position2D& value)
		{
			if (context.eSpec == VspFormatSpec::FixedFloat)
			{
				out.Append(VspFormat::Format("{}", value.fPositionX));
				out.Append(", ");
				out.Append(VspFormat::Format("{}", value.fPositionY));
				return;
			}

			out.Append("(");
			out.Append(VspFormat::Format("{}", value.fPositionX));
			out.Append(", ");
			out.Append(VspFormat::Format("{}", value.fPositionY));
			out.Append(")");
		}
	};
}
