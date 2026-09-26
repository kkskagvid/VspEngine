#pragma once

#include "Core/Core.h"
#include "Core/String/VspStringFormat.h"

namespace Vsp
{
	// -------------------------------------------------------------------------
	// Small value types the script host shares with the engine
	// -------------------------------------------------------------------------
	// The frame clock itself lives in Classes/Time; what remains here are the
	// plain values the host moves across its own boundary and logs - today the
	// 2D position a frame capture reports.
	// -------------------------------------------------------------------------

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
