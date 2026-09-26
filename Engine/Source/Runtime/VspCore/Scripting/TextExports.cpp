#include "RuntimePCH.h"

#include <cstring>
#include <vector>

#include "Core/Diagnostics/ErrorHandling.h"
#include "Core/Text/TextSystem.h"
#include "Scripting/ScriptExport.h"

// -------------------------------------------------------------------------
// Text exports consumed by managed code (C# -> C++ direction).
// VspEngine.UI.Text and VspEngine.UI.Font P/Invoke these exact names.
//
// The engine owns text: a font is loaded once into Core/Text/TextSystem and
// addressed by a 1-based handle afterwards, the same scheme the scene uses for
// its objects. Laying a run out produces glyph quads in text pixel space plus
// the font's coverage atlas, which is all a UI renderer needs.
//
// Quad layout, eight floats per quad:
//   [0] left [1] top [2] right [3] bottom [4] u0 [5] v0 [6] u1 [7] v1
// (left, top) is the glyph rectangle's top-left corner in text pixel space -
// +X right, +Y down, the origin at the pen position of the first line - and
// the UVs address the atlas VspText_CopyAtlasPixels hands out, whose texels are
// white with the coverage in the alpha channel, so a renderer colours the text
// by multiplying the sampled texel with the text colour.
//
// Every export validates its handle through TextSystem::FindFont and answers
// with the empty value (0) after logging when the caller passes nothing usable.
// -------------------------------------------------------------------------

static constexpr const char* kLogTag = "TextExports";

// -------------------------------------------------------------------------
// Fonts
// -------------------------------------------------------------------------

CSHARP_EXPORT uint32 VspText_CreateFont(const char* pFilePathUtf8, float fPixelSize)
{
	if (pFilePathUtf8 == nullptr)
	{
		VSP_RETURN_EMPTY(0u, kLogTag, "VspText_CreateFont was given a null path.");
	}
	return Vsp::TextSystem::Get().CreateFont(Vsp::VspString(pFilePathUtf8), fPixelSize);
}

CSHARP_EXPORT uint32 VspText_CreateDefaultFont(float fPixelSize)
{
	return Vsp::TextSystem::Get().CreateDefaultFont(fPixelSize);
}

CSHARP_EXPORT void VspText_DestroyFont(uint32 uFontHandle)
{
	// DestroyFont validates the handle itself and logs the ones that address
	// nothing, so there is no second lookup here.
	Vsp::TextSystem::Get().DestroyFont(uFontHandle);
}

CSHARP_EXPORT int32 VspText_IsFontValid(uint32 uFontHandle)
{
	// Asking whether a handle is valid is expected to answer 0 for a stale or
	// zero handle: that is the answer, not a failure, so nothing is logged.
	return Vsp::TextSystem::Get().FindFont(uFontHandle) != nullptr ? 1 : 0;
}

// -------------------------------------------------------------------------
// Metrics
// -------------------------------------------------------------------------

CSHARP_EXPORT float VspText_GetLineHeight(uint32 uFontHandle)
{
	const Vsp::Font* pFont = Vsp::TextSystem::Get().FindFont(uFontHandle);
	if (pFont == nullptr)
	{
		VSP_RETURN_EMPTY(0.0f, kLogTag, "Font handle {} does not address a live font.", uFontHandle);
	}
	return pFont->GetLineHeight();
}

CSHARP_EXPORT float VspText_GetAscent(uint32 uFontHandle)
{
	const Vsp::Font* pFont = Vsp::TextSystem::Get().FindFont(uFontHandle);
	if (pFont == nullptr)
	{
		VSP_RETURN_EMPTY(0.0f, kLogTag, "Font handle {} does not address a live font.", uFontHandle);
	}
	return pFont->GetAscent();
}

CSHARP_EXPORT float VspText_GetDescent(uint32 uFontHandle)
{
	const Vsp::Font* pFont = Vsp::TextSystem::Get().FindFont(uFontHandle);
	if (pFont == nullptr)
	{
		VSP_RETURN_EMPTY(0.0f, kLogTag, "Font handle {} does not address a live font.", uFontHandle);
	}
	return pFont->GetDescent();
}

// -------------------------------------------------------------------------
// Measurement and layout
// -------------------------------------------------------------------------

CSHARP_EXPORT int32 VspText_MeasureText(uint32 uFontHandle, const char* pTextUtf8, float* pOutSize2)
{
	if (pTextUtf8 == nullptr || pOutSize2 == nullptr)
	{
		VSP_RETURN_EMPTY(0, kLogTag, "VspText_MeasureText was given a null text or size buffer.");
	}

	// A failed measurement leaves the caller a defined result instead of
	// whatever its buffer happened to hold.
	pOutSize2[0] = 0.0f;
	pOutSize2[1] = 0.0f;

	Vsp::Font* pFont = Vsp::TextSystem::Get().FindFont(uFontHandle);
	if (pFont == nullptr)
	{
		VSP_RETURN_EMPTY(0, kLogTag, "Font handle {} does not address a live font.", uFontHandle);
	}

	float fWidthPixels = 0.0f;
	float fHeightPixels = 0.0f;
	if (!pFont->MeasureText(Vsp::VspString(pTextUtf8), fWidthPixels, fHeightPixels))
	{
		// MeasureText has logged the reason.
		return 0;
	}

	pOutSize2[0] = fWidthPixels;
	pOutSize2[1] = fHeightPixels;
	return 1;
}

CSHARP_EXPORT uint32 VspText_LayoutText(uint32 uFontHandle, const char* pTextUtf8, float* pOutQuads, uint32 uMaxQuadCount)
{
	if (pTextUtf8 == nullptr)
	{
		VSP_RETURN_EMPTY(0u, kLogTag, "VspText_LayoutText was given a null text.");
	}

	Vsp::Font* pFont = Vsp::TextSystem::Get().FindFont(uFontHandle);
	if (pFont == nullptr)
	{
		VSP_RETURN_EMPTY(0u, kLogTag, "Font handle {} does not address a live font.", uFontHandle);
	}

	const Vsp::VspString sText(pTextUtf8);
	if (pOutQuads == nullptr || uMaxQuadCount == 0)
	{
		// Nothing to fill: the caller only wants to know how many quads the run
		// needs, which Font::LayoutText reports without writing anything.
		return pFont->LayoutText(sText, nullptr, 0u);
	}

	// A native quad carries more than the eight floats the managed side
	// declares (it also remembers the code point it was shaped from), so the run
	// is laid out into native quads first and then flattened into the caller's
	// array, which keeps the two layouts free to differ.
	std::vector<Vsp::TextGlyphQuad> quads(static_cast<size_t>(uMaxQuadCount));
	const uint32 uTotalQuadCount = pFont->LayoutText(sText, quads.data(), uMaxQuadCount);
	const uint32 uWrittenQuadCount = (uTotalQuadCount < uMaxQuadCount) ? uTotalQuadCount : uMaxQuadCount;

	for (uint32 uQuadIndex = 0; uQuadIndex < uWrittenQuadCount; ++uQuadIndex)
	{
		const Vsp::TextGlyphQuad& quad = quads[uQuadIndex];
		float* pOutQuad = pOutQuads + static_cast<size_t>(uQuadIndex) * 8u;
		pOutQuad[0] = quad.fLeft;
		pOutQuad[1] = quad.fTop;
		pOutQuad[2] = quad.fRight;
		pOutQuad[3] = quad.fBottom;
		pOutQuad[4] = quad.fU0;
		pOutQuad[5] = quad.fV0;
		pOutQuad[6] = quad.fU1;
		pOutQuad[7] = quad.fV1;
	}

	// The total, so a caller that passed a buffer which is too small knows how
	// much room the run really needs.
	return uTotalQuadCount;
}

// -------------------------------------------------------------------------
// Atlas
// -------------------------------------------------------------------------

CSHARP_EXPORT int32 VspText_GetAtlasWidth(uint32 uFontHandle)
{
	const Vsp::Font* pFont = Vsp::TextSystem::Get().FindFont(uFontHandle);
	if (pFont == nullptr)
	{
		VSP_RETURN_EMPTY(0, kLogTag, "Font handle {} does not address a live font.", uFontHandle);
	}
	return static_cast<int32>(pFont->GetAtlasWidth());
}

CSHARP_EXPORT int32 VspText_GetAtlasHeight(uint32 uFontHandle)
{
	const Vsp::Font* pFont = Vsp::TextSystem::Get().FindFont(uFontHandle);
	if (pFont == nullptr)
	{
		VSP_RETURN_EMPTY(0, kLogTag, "Font handle {} does not address a live font.", uFontHandle);
	}
	return static_cast<int32>(pFont->GetAtlasHeight());
}

CSHARP_EXPORT uint32 VspText_GetAtlasVersion(uint32 uFontHandle)
{
	const Vsp::Font* pFont = Vsp::TextSystem::Get().FindFont(uFontHandle);
	if (pFont == nullptr)
	{
		VSP_RETURN_EMPTY(0u, kLogTag, "Font handle {} does not address a live font.", uFontHandle);
	}
	return pFont->GetAtlasVersion();
}

CSHARP_EXPORT int32 VspText_CopyAtlasPixels(uint32 uFontHandle, uint8* pOutPixelsRgba8, uint32 uBufferCapacityBytes)
{
	Vsp::Font* pFont = Vsp::TextSystem::Get().FindFont(uFontHandle);
	if (pFont == nullptr)
	{
		VSP_RETURN_EMPTY(0, kLogTag, "Font handle {} does not address a live font.", uFontHandle);
	}

	if (pOutPixelsRgba8 == nullptr)
	{
		VSP_RETURN_EMPTY(0, kLogTag, "VspText_CopyAtlasPixels was given a null buffer.");
	}

	const uint8* pAtlasPixels = pFont->GetAtlasPixels();
	const size_t nAtlasByteCount = static_cast<size_t>(pFont->GetAtlasWidth()) * static_cast<size_t>(pFont->GetAtlasHeight()) * 4u;
	if (pAtlasPixels == nullptr || nAtlasByteCount == 0)
	{
		VSP_RETURN_EMPTY(0, kLogTag, "Font handle {} has no atlas to copy.", uFontHandle);
	}

	// The whole atlas or nothing: a partially filled texture would be uploaded
	// as if it were complete.
	if (static_cast<size_t>(uBufferCapacityBytes) < nAtlasByteCount)
	{
		VSP_RETURN_EMPTY(0, kLogTag, "The atlas of font {} needs {} byte(s), but the buffer holds only {}.",
			uFontHandle, nAtlasByteCount, uBufferCapacityBytes);
	}

	memcpy(pOutPixelsRgba8, pAtlasPixels, nAtlasByteCount);
	return static_cast<int32>(nAtlasByteCount);
}
