#pragma once

#include "Core/Core.h"
#include "Core/String/VspStringFormat.h"

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
	// The frame clock managed scripts read through Time.DeltaTime /
	// Time.ElapsedTime. It is the only per-frame state the script host still
	// owns: everything else a script touches (transforms, components, render
	// state) lives in the native scene (Classes/Scene), and rendering is driven
	// by the managed render pipeline through the wrapped graphics API.
	// The native exports (NativeExports.cpp) forward C# calls into this class.
	// -------------------------------------------------------------------------
	class RUNTIME_API ScriptCore
	{
	public:
		static ScriptCore& Get();

		// -------- Time --------
		void SetDeltaTime(float fDeltaSeconds) { m_fDeltaTime = fDeltaSeconds; }
		float GetDeltaTime() const { return m_fDeltaTime; }

		void SetElapsedTime(float fElapsedSeconds) { m_fElapsedTime = fElapsedSeconds; }
		float GetElapsedTime() const { return m_fElapsedTime; }

	private:
		ScriptCore() = default;

		float m_fDeltaTime = 0.0f;
		float m_fElapsedTime = 0.0f;
	};
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
