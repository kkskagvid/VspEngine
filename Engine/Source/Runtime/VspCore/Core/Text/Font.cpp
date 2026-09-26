#include "RuntimePCH.h"

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <utility>

#include <ft2build.h>
#include FT_FREETYPE_H

#include "Core/Diagnostics/ErrorHandling.h"
#include "Core/Text/Font.h"

// -------------------------------------------------------------------------
// Font
// -------------------------------------------------------------------------
// The text rasterizer: a FreeType face and the RGBA8 coverage atlas the
// rasterized glyphs are packed into. FreeType is the only text engine this
// module uses - it maps code points to glyphs, reports the advances and the
// legacy 'kern' pair adjustments, and rasterizes the bitmaps.
//
// Design decisions worth knowing:
//   * One FT_Library is shared by every Font. It carries the driver state, not
//     per-font state, and initializing it once takes it off the load path.
//   * A face created from a file owns its bytes (m_FontFileBytes), because
//     FreeType borrows the buffer the face is created from for as long as the
//     face lives; a face created from memory borrows the caller's buffer.
//   * The atlas is a fixed k_nDefaultAtlasSize square that stays transparent
//     until a glyph is rasterized into it. RGB is white wherever a glyph was
//     written and the coverage sits in the alpha channel, so a renderer gets
//     coloured text by multiplying the sampled texel with the vertex colour.
//     m_uAtlasVersion changes whenever the content did, which tells a caller
//     that the texture it uploaded is stale.
//   * Bitmaps are packed by a shelf packer with one pixel of padding between
//     entries, so a bilinear sample at a glyph edge cannot reach its neighbour.
//   * Shaping walks the run itself: one code point at a time, mapped through
//     FT_Get_Char_Index, positioned by the glyph advance and by 'kern' pairs.
//     The engine's UI text is short and left-to-right, so the few hundred lines
//     of a shaping library are not warranted; complex scripts would need a real
//     shaper, which is the point at which this walk gets replaced.
//   * Nothing here throws: a failure is logged and reported as the empty value.
// -------------------------------------------------------------------------

namespace Vsp
{
	static constexpr const char* kLogTag = "Font";

	// Padding between two atlas entries.
	static constexpr uint32 k_nAtlasPadding = 1u;

	namespace
	{
		// -------- FreeType library --------

		// Builds the process-wide FreeType library. FreeType is initialized once
		// for the whole runtime: the library object holds the driver state every
		// face needs, and building it is the expensive part of the first load.
		FT_Library CreateSharedLibrary()
		{
			FT_Library pLibrary = nullptr;
			if (FT_Init_FreeType(&pLibrary) != 0)
			{
				return nullptr;
			}
			return pLibrary;
		}

		// The one library every font uses. C++ initializes the static on the
		// first call and runs exactly one initialization, even if it fails.
		FT_Library GetSharedLibrary()
		{
			static FT_Library s_pLibrary = CreateSharedLibrary();
			return s_pLibrary;
		}

		// -------- Small helpers --------

		// Keeps a requested pixel size inside the range the atlas can serve.
		float ClampPixelSize(float fPixelSize)
		{
			if (fPixelSize < Font::k_fMinimumPixelSize)
			{
				return Font::k_fMinimumPixelSize;
			}
			if (fPixelSize > Font::k_fMaximumPixelSize)
			{
				return Font::k_fMaximumPixelSize;
			}
			return fPixelSize;
		}

		// Reads one coverage sample out of a rendered FreeType bitmap.
		uint8 ReadBitmapCoverage(const FT_Bitmap& Bitmap, const uint8* pSourceRow, uint32 uColumn)
		{
			if (Bitmap.pixel_mode == FT_PIXEL_MODE_MONO)
			{
				// A monochrome strike (the embedded bitmaps of an old CJK face)
				// packs eight pixels per byte, most significant bit first, and
				// knows no coverage levels: a pixel is on or off.
				return (pSourceRow[uColumn >> 3] & (0x80u >> (uColumn & 7u))) != 0 ? 255u : 0u;
			}
			return pSourceRow[uColumn];
		}

		// Copies a rendered bitmap into the atlas: the coverage becomes the
		// alpha channel and the RGB channels become white, so the glyph takes
		// whatever colour a caller multiplies in. The one-pixel border the
		// packer reserved around the glyph is written as transparent white,
		// which keeps a bilinear sample at the edge from darkening the glyph.
		void CopyCoverageToAtlas(const FT_Bitmap& Bitmap, uint8* pAtlasPixels, uint32 uAtlasWidth, uint32 uAtlasX, uint32 uAtlasY)
		{
			const uint32 uBlockWidth = Bitmap.width + k_nAtlasPadding;
			const uint32 uBlockHeight = Bitmap.rows + k_nAtlasPadding;

			for (uint32 uRow = 0; uRow < uBlockHeight; ++uRow)
			{
				uint8* pDestinationRow = pAtlasPixels + (static_cast<size_t>(uAtlasY + uRow) * uAtlasWidth + uAtlasX) * 4u;

				// The bottom row of the block is padding and has no source row.
				// FreeType keeps the first row at Bitmap.buffer even when the
				// pitch is negative, so walking by the pitch is always right.
				const uint8* pSourceRow = nullptr;
				if (uRow < Bitmap.rows)
				{
					pSourceRow = Bitmap.buffer + static_cast<ptrdiff_t>(uRow) * static_cast<ptrdiff_t>(Bitmap.pitch);
				}

				for (uint32 uColumn = 0; uColumn < uBlockWidth; ++uColumn)
				{
					uint8* pTexel = pDestinationRow + static_cast<size_t>(uColumn) * 4u;
					pTexel[0] = 255u;
					pTexel[1] = 255u;
					pTexel[2] = 255u;
					pTexel[3] = 0u;

					if (pSourceRow != nullptr && uColumn < Bitmap.width)
					{
						pTexel[3] = ReadBitmapCoverage(Bitmap, pSourceRow, uColumn);
					}
				}
			}
		}

		// Decodes the UTF-8 code point at a byte offset and reports how many
		// bytes it took, so the caller always makes progress: a malformed byte
		// decodes to U+FFFD and is consumed on its own, exactly like everywhere
		// else in the engine.
		uint32 DecodeUtf8CodePoint(const char* pUtf8Bytes, size_t nByteCount, size_t nByteOffset, size_t& outByteCount)
		{
			outByteCount = 0;
			if (nByteOffset >= nByteCount)
			{
				return 0u;
			}

			const uint8* pLeadByte = reinterpret_cast<const uint8*>(pUtf8Bytes + nByteOffset);
			if (pLeadByte[0] < 0x80u)
			{
				outByteCount = 1;
				return pLeadByte[0];
			}

			size_t nSequenceByteCount = 0;
			uint32 uCodePoint = 0;
			if ((pLeadByte[0] & 0xE0u) == 0xC0u)
			{
				nSequenceByteCount = 2;
				uCodePoint = pLeadByte[0] & 0x1Fu;
			}
			else if ((pLeadByte[0] & 0xF0u) == 0xE0u)
			{
				nSequenceByteCount = 3;
				uCodePoint = pLeadByte[0] & 0x0Fu;
			}
			else if ((pLeadByte[0] & 0xF8u) == 0xF0u)
			{
				nSequenceByteCount = 4;
				uCodePoint = pLeadByte[0] & 0x07u;
			}
			else
			{
				outByteCount = 1;
				return 0xFFFDu;
			}

			if (nByteOffset + nSequenceByteCount > nByteCount)
			{
				outByteCount = 1;
				return 0xFFFDu;
			}

			for (size_t nSequenceByteIndex = 1; nSequenceByteIndex < nSequenceByteCount; ++nSequenceByteIndex)
			{
				const uint8 uContinuationByte = pLeadByte[nSequenceByteIndex];
				if ((uContinuationByte & 0xC0u) != 0x80u)
				{
					outByteCount = 1;
					return 0xFFFDu;
				}
				uCodePoint = (uCodePoint << 6) | (uContinuationByte & 0x3Fu);
			}

			outByteCount = nSequenceByteCount;
			return uCodePoint;
		}

		// -------- Shaping --------

		// One glyph of the run plus the pen position it was placed at.
		struct ShapedGlyph
		{
			uint32 uGlyphIndex = 0;
			uint32 uCodePoint = 0;
			float fPenX = 0.0f;
			float fPenY = 0.0f;
			float fAdvanceX = 0.0f;
		};

		// The shaping walk MeasureText and LayoutText share: every line of the
		// UTF-8 run is turned into glyphs, and each glyph is handed to the
		// visitor together with the pen position it belongs to.
		//
		// '\n' starts a new line; inside a line the pen moves right by the glyph
		// advances (plus the legacy 'kern' adjustment of the pair), and a new line
		// moves it down by the line height. The visitor therefore sees text pixel
		// space: +X right, +Y down, the origin at the pen position of the first
		// line. An empty run is one empty line.
		//
		// A code point the face does not cover maps to glyph 0, the .notdef box,
		// and is still laid out: a missing character has to be visible rather
		// than silently disappear.
		template <typename VisitorType>
		void VisitShapedGlyphs(FT_Face pFace, const char* pTextBytes, size_t nTextByteCount, float fLineHeight, VisitorType&& Visitor)
		{
			float fPenY = 0.0f;
			size_t nLineStartByteOffset = 0;
			while (true)
			{
				size_t nLineEndByteOffset = nLineStartByteOffset;
				while (nLineEndByteOffset < nTextByteCount && pTextBytes[nLineEndByteOffset] != '\n')
				{
					++nLineEndByteOffset;
				}

				float fPenX = 0.0f;
				uint32 uPreviousGlyphIndex = 0;
				bool bHasPreviousGlyph = false;

				size_t nByteOffset = nLineStartByteOffset;
				while (nByteOffset < nLineEndByteOffset)
				{
					size_t nCodePointByteCount = 0;
					const uint32 uCodePoint = DecodeUtf8CodePoint(pTextBytes, nTextByteCount, nByteOffset, nCodePointByteCount);
					nByteOffset += nCodePointByteCount;

					const uint32 uGlyphIndex = FT_Get_Char_Index(pFace, uCodePoint);

					// Legacy kerning from the face's 'kern' table, when it has
					// one. The OpenType pair adjustment lives in GPOS and needs a
					// shaper, which this module deliberately does not carry.
					if (bHasPreviousGlyph && FT_HAS_KERNING(pFace))
					{
						FT_Vector kerning = {};
						if (FT_Get_Kerning(pFace, uPreviousGlyphIndex, uGlyphIndex, FT_KERNING_DEFAULT, &kerning) == 0)
						{
							fPenX += static_cast<float>(kerning.x) / 64.0f;
						}
					}

					// The advance is read with a plain load; the bitmap comes
					// from GetGlyphEntry, which loads the glyph again with
					// FT_LOAD_RENDER. FreeType reports both in 26.6 pixels, so 64
					// units are one pixel.
					float fAdvanceX = 0.0f;
					if (FT_Load_Glyph(pFace, uGlyphIndex, FT_LOAD_DEFAULT) != 0)
					{
						// Keep the glyph in the run (an empty quad) rather than
						// shifting everything after it.
						LOG_ERROR(kLogTag, "Glyph {} could not be measured; it is laid out as an empty quad.", uGlyphIndex);
					}
					else
					{
						fAdvanceX = static_cast<float>(pFace->glyph->advance.x) / 64.0f;
					}

					ShapedGlyph shapedGlyph;
					shapedGlyph.uGlyphIndex = uGlyphIndex;
					shapedGlyph.uCodePoint = uCodePoint;
					shapedGlyph.fPenX = fPenX;
					shapedGlyph.fPenY = fPenY;
					shapedGlyph.fAdvanceX = fAdvanceX;

					Visitor(shapedGlyph);
					fPenX += fAdvanceX;
					uPreviousGlyphIndex = uGlyphIndex;
					bHasPreviousGlyph = true;
				}

				fPenY += fLineHeight;

				if (nLineEndByteOffset >= nTextByteCount)
				{
					break;
				}
				nLineStartByteOffset = nLineEndByteOffset + 1;
			}
		}
	}

	// -------------------------------------------------------------------------
	// Lifetime
	// -------------------------------------------------------------------------

	Font::Font() = default;

	Font::~Font()
	{
		Unload();
	}

	Font::Font(Font&& Other) noexcept
	{
		*this = std::move(Other);
	}

	Font& Font::operator=(Font&& Other) noexcept
	{
		if (this == &Other)
		{
			return *this;
		}

		// Whatever this font held is released before it takes the other's face.
		Unload();

		m_pFaceData = Other.m_pFaceData;
		m_sSourcePath = std::move(Other.m_sSourcePath);
		m_FontFileBytes = std::move(Other.m_FontFileBytes);
		m_fPixelSize = Other.m_fPixelSize;
		m_fAscent = Other.m_fAscent;
		m_fDescent = Other.m_fDescent;
		m_fLineHeight = Other.m_fLineHeight;
		m_GlyphEntries = std::move(Other.m_GlyphEntries);
		m_nAtlasWidth = Other.m_nAtlasWidth;
		m_nAtlasHeight = Other.m_nAtlasHeight;
		m_AtlasPixels = std::move(Other.m_AtlasPixels);
		m_nShelfX = Other.m_nShelfX;
		m_nShelfY = Other.m_nShelfY;
		m_nShelfHeight = Other.m_nShelfHeight;
		m_uAtlasVersion = Other.m_uAtlasVersion;
		m_bAtlasOverflowReported = Other.m_bAtlasOverflowReported;

		// The moved-from font owns nothing any more, so destroying it is a
		// no-op and it can be loaded again.
		Other.m_pFaceData = nullptr;
		Other.m_sSourcePath = nullptr;
		Other.m_fPixelSize = 0.0f;
		Other.m_fAscent = 0.0f;
		Other.m_fDescent = 0.0f;
		Other.m_fLineHeight = 0.0f;
		Other.m_nAtlasWidth = 0;
		Other.m_nAtlasHeight = 0;
		Other.m_nShelfX = 0;
		Other.m_nShelfY = 0;
		Other.m_nShelfHeight = 0;
		Other.m_uAtlasVersion = 0;
		Other.m_bAtlasOverflowReported = false;

		return *this;
	}

	// -------------------------------------------------------------------------
	// Loading
	// -------------------------------------------------------------------------

	bool Font::LoadFromFile(const VspString& sFilePathUtf8, float fPixelSize)
	{
		// The file is read into a local buffer first, so a missing or unusable
		// file leaves the font that was loaded before untouched.
		std::vector<uint8> fontFileBytes;

		FILE* pFile = nullptr;
		fopen_s(&pFile, sFilePathUtf8.GetData(), "rb");
		if (pFile == nullptr)
		{
			VSP_RETURN_EMPTY(false, kLogTag, "The font file '{}' could not be opened.", sFilePathUtf8.GetData());
		}

		uint8 sReadBuffer[8192];
		size_t nReadByteCount = 0;
		while ((nReadByteCount = fread(sReadBuffer, 1, sizeof(sReadBuffer), pFile)) > 0)
		{
			fontFileBytes.insert(fontFileBytes.end(), sReadBuffer, sReadBuffer + nReadByteCount);
		}
		fclose(pFile);

		if (fontFileBytes.empty())
		{
			VSP_RETURN_EMPTY(false, kLogTag, "The font file '{}' holds no byte.", sFilePathUtf8.GetData());
		}

		if (!LoadFromMemory(fontFileBytes.data(), fontFileBytes.size(), fPixelSize))
		{
			// LoadFromMemory has logged the reason and left the font unloaded.
			return false;
		}

		// The face borrows the bytes, so the font keeps the allocation alive for
		// as long as the face lives. Moving the vector hands the very same
		// buffer over, which is what keeps the borrowed pointer valid.
		m_FontFileBytes = std::move(fontFileBytes);
		m_sSourcePath = sFilePathUtf8;

		LOG_DEBUG(kLogTag, "The font '{}' is ready at {} px.", m_sSourcePath.GetData(), m_fPixelSize);
		return true;
	}

	bool Font::LoadFromMemory(const void* pFontData, size_t nByteCount, float fPixelSize)
	{
		if (pFontData == nullptr || nByteCount == 0)
		{
			VSP_RETURN_EMPTY(false, kLogTag, "LoadFromMemory was given no font data.");
		}

		// Whatever the font held before goes first: the old face borrowed its
		// own buffer and the atlas was rasterized from that face. When this call
		// comes from LoadFromFile, pFontData points into a local buffer, so
		// releasing the owned one here cannot pull the ground away.
		Unload();

		FT_Library pLibrary = GetSharedLibrary();
		if (pLibrary == nullptr)
		{
			VSP_RETURN_EMPTY(false, kLogTag, "FreeType could not be initialized.");
		}

		FT_Face pFace = nullptr;
		const FT_Error nFaceError = FT_New_Memory_Face(pLibrary, static_cast<const FT_Byte*>(pFontData),
			static_cast<FT_Long>(nByteCount), 0, &pFace);
		if (nFaceError != 0)
		{
			VSP_RETURN_EMPTY(false, kLogTag, "The {} byte(s) of font data are not a usable face (FreeType error {}).", nByteCount, nFaceError);
		}
		m_pFaceData = pFace;

		// The pixel size is what every later step depends on: the metrics and
		// the rasterization both read it from the face.
		const float fClampedPixelSize = ClampPixelSize(fPixelSize);
		const FT_Error nSizeError = FT_Set_Pixel_Sizes(pFace, 0, static_cast<FT_UInt>(fClampedPixelSize));
		if (nSizeError != 0)
		{
			Unload();
			VSP_RETURN_EMPTY(false, kLogTag, "The face cannot be sized to {} px (FreeType error {}).", fClampedPixelSize, nSizeError);
		}
		m_fPixelSize = fClampedPixelSize;

		UpdateMetrics();
		ResetAtlas();
		return true;
	}

	void Font::Unload()
	{
		if (m_pFaceData != nullptr)
		{
			FT_Done_Face(static_cast<FT_Face>(m_pFaceData));
			m_pFaceData = nullptr;
		}

		// The face borrowed these bytes and is gone, so they can go too.
		m_FontFileBytes.clear();
		m_FontFileBytes.shrink_to_fit();

		m_sSourcePath = nullptr;
		m_fPixelSize = 0.0f;
		m_fAscent = 0.0f;
		m_fDescent = 0.0f;
		m_fLineHeight = 0.0f;

		m_GlyphEntries.Clear();

		m_nAtlasWidth = 0;
		m_nAtlasHeight = 0;
		m_AtlasPixels = nullptr;
		m_nShelfX = 0;
		m_nShelfY = 0;
		m_nShelfHeight = 0;
		m_bAtlasOverflowReported = false;
	}

	void Font::SetPixelSize(float fPixelSize)
	{
		const float fClampedPixelSize = ClampPixelSize(fPixelSize);
		if (fClampedPixelSize == m_fPixelSize)
		{
			return;
		}
		m_fPixelSize = fClampedPixelSize;

		FT_Face pFace = static_cast<FT_Face>(m_pFaceData);
		if (pFace == nullptr)
		{
			// There is no face to resize yet; the size is remembered and the
			// next load prepares the face with it.
			return;
		}

		const FT_Error nSizeError = FT_Set_Pixel_Sizes(pFace, 0, static_cast<FT_UInt>(fClampedPixelSize));
		if (nSizeError != 0)
		{
			VSP_RETURN_VOID(kLogTag, "The face cannot be resized to {} px (FreeType error {}).", fClampedPixelSize, nSizeError);
		}

		UpdateMetrics();

		// Everything the atlas holds was rasterized at the old size.
		m_GlyphEntries.Clear();
		ResetAtlas();
	}

	// -------------------------------------------------------------------------
	// Metrics
	// -------------------------------------------------------------------------

	void Font::UpdateMetrics()
	{
		FT_Face pFace = static_cast<FT_Face>(m_pFaceData);
		if (pFace == nullptr || pFace->size == nullptr)
		{
			m_fAscent = 0.0f;
			m_fDescent = 0.0f;
			m_fLineHeight = 0.0f;
			return;
		}

		// FreeType reports the metrics in 26.6 pixels and keeps the descender
		// negative, while the text layout works with +Y pointing down.
		const FT_Size_Metrics& Metrics = pFace->size->metrics;
		m_fAscent = static_cast<float>(Metrics.ascender) / 64.0f;
		m_fDescent = -static_cast<float>(Metrics.descender) / 64.0f;

		// A face that reports no height still has to lay out on whole lines.
		m_fLineHeight = static_cast<float>(Metrics.height) / 64.0f;
		if (m_fLineHeight <= 0.0f)
		{
			m_fLineHeight = m_fAscent + m_fDescent;
		}
	}

	// -------------------------------------------------------------------------
	// Measurement and layout
	// -------------------------------------------------------------------------

	bool Font::MeasureText(const VspString& sTextUtf8, float& outWidthPixels, float& outHeightPixels)
	{
		outWidthPixels = 0.0f;
		outHeightPixels = 0.0f;

		FT_Face pFace = static_cast<FT_Face>(m_pFaceData);
		if (pFace == nullptr)
		{
			VSP_RETURN_EMPTY(false, kLogTag, "MeasureText was called on a font that is not loaded.");
		}

		// The widest line is what a caller has to make room for: every glyph
		// adds its advance to the line pen, so the pen at the end of a line is
		// that line's width.
		float fWidestLine = 0.0f;
		auto Visitor = [&fWidestLine](const ShapedGlyph& shapedGlyph)
		{
			const float fLineWidth = shapedGlyph.fPenX + shapedGlyph.fAdvanceX;
			if (fLineWidth > fWidestLine)
			{
				fWidestLine = fLineWidth;
			}
		};

		VisitShapedGlyphs(pFace, sTextUtf8.GetData(), sTextUtf8.GetByteLength(), m_fLineHeight, Visitor);

		// Every text is at least one line high - an empty one is one empty line -
		// and each '\n' starts another one.
		uint32 nLineCount = 1;
		const char* pTextBytes = sTextUtf8.GetData();
		const size_t nTextByteCount = sTextUtf8.GetByteLength();
		for (size_t nByteIndex = 0; nByteIndex < nTextByteCount; ++nByteIndex)
		{
			if (pTextBytes[nByteIndex] == '\n')
			{
				++nLineCount;
			}
		}

		outWidthPixels = fWidestLine;
		outHeightPixels = static_cast<float>(nLineCount) * m_fLineHeight;
		return true;
	}

	uint32 Font::LayoutText(const VspString& sTextUtf8, TextGlyphQuad* pOutQuads, uint32 uMaxQuadCount)
	{
		FT_Face pFace = static_cast<FT_Face>(m_pFaceData);
		if (pFace == nullptr)
		{
			LOG_ERROR(kLogTag, "LayoutText was called on a font that is not loaded.");
			return 0;
		}

		uint32 uTotalQuadCount = 0;
		auto Visitor = [this, pOutQuads, uMaxQuadCount, &uTotalQuadCount](const ShapedGlyph& shapedGlyph)
		{
			// One quad per glyph, in reading order, so a caller can map a quad
			// index back to the glyph it came from.
			TextGlyphQuad quad;
			quad.uCodePoint = shapedGlyph.uCodePoint;

			const GlyphEntry* pEntry = GetGlyphEntry(shapedGlyph.uGlyphIndex);
			if (pEntry != nullptr)
			{
				// The bearing is measured from the pen, with +Y down: a glyph
				// whose bitmap sits above the pen line has a negative bearing.
				quad.fLeft = shapedGlyph.fPenX + pEntry->fBearingX;
				quad.fTop = shapedGlyph.fPenY + pEntry->fBearingY;
				quad.fRight = quad.fLeft + pEntry->fWidth;
				quad.fBottom = quad.fTop + pEntry->fHeight;
				quad.fU0 = pEntry->fU0;
				quad.fV0 = pEntry->fV0;
				quad.fU1 = pEntry->fU1;
				quad.fV1 = pEntry->fV1;
			}
			// A glyph the atlas did not take keeps an empty rectangle: it costs
			// no pixels but holds its place in the run.

			if (pOutQuads != nullptr && uTotalQuadCount < uMaxQuadCount)
			{
				pOutQuads[uTotalQuadCount] = quad;
			}
			++uTotalQuadCount;
		};

		VisitShapedGlyphs(pFace, sTextUtf8.GetData(), sTextUtf8.GetByteLength(), m_fLineHeight, Visitor);
		return uTotalQuadCount;
	}

	// -------------------------------------------------------------------------
	// Glyph rasterization and the atlas
	// -------------------------------------------------------------------------

	const Font::GlyphEntry* Font::GetGlyphEntry(uint32 uGlyphIndex)
	{
		// The cache holds one entry per distinct glyph a run used, and this runs
		// once per glyph, so a linear scan is the cheapest thing that works.
		for (size_t nEntryIndex = 0; nEntryIndex < m_GlyphEntries.GetSize(); ++nEntryIndex)
		{
			if (m_GlyphEntries[nEntryIndex].uGlyphIndex == uGlyphIndex)
			{
				return &m_GlyphEntries[nEntryIndex];
			}
		}

		FT_Face pFace = static_cast<FT_Face>(m_pFaceData);
		if (pFace == nullptr)
		{
			return nullptr;
		}

		// FT_LOAD_RENDER rasterizes into the glyph slot right away, in the
		// face's render mode, which FT_LOAD_TARGET_NORMAL pins to 8-bit
		// coverage. Hinting stays on, so the bitmaps are the ones FreeType
		// produces for the face's pixel size.
		const FT_Error nLoadError = FT_Load_Glyph(pFace, uGlyphIndex, FT_LOAD_DEFAULT | FT_LOAD_RENDER | FT_LOAD_TARGET_NORMAL);
		if (nLoadError != 0)
		{
			VSP_LOG_ERROR(kLogTag, "Glyph {} could not be rasterized (FreeType error {}).", uGlyphIndex, nLoadError);
			return nullptr;
		}

		const FT_GlyphSlot pGlyph = pFace->glyph;
		const FT_Bitmap& Bitmap = pGlyph->bitmap;

		GlyphEntry entry;
		entry.uGlyphIndex = uGlyphIndex;
		entry.fBearingX = static_cast<float>(pGlyph->bitmap_left);
		entry.fBearingY = -static_cast<float>(pGlyph->bitmap_top);

		if (Bitmap.width > 0 && Bitmap.rows > 0)
		{
			uint32 uAtlasX = 0;
			uint32 uAtlasY = 0;
			if (!ReserveAtlasRegion(Bitmap.width, Bitmap.rows, uAtlasX, uAtlasY))
			{
				// The atlas is full. The glyph keeps no entry, so a later run
				// asks again; the failure itself was reported by the packer.
				return nullptr;
			}

			CopyCoverageToAtlas(Bitmap, m_AtlasPixels.GetData(), m_nAtlasWidth, uAtlasX, uAtlasY);

			const float fAtlasWidth = static_cast<float>(m_nAtlasWidth);
			const float fAtlasHeight = static_cast<float>(m_nAtlasHeight);
			entry.fWidth = static_cast<float>(Bitmap.width);
			entry.fHeight = static_cast<float>(Bitmap.rows);
			entry.fU0 = static_cast<float>(uAtlasX) / fAtlasWidth;
			entry.fV0 = static_cast<float>(uAtlasY) / fAtlasHeight;
			entry.fU1 = static_cast<float>(uAtlasX + Bitmap.width) / fAtlasWidth;
			entry.fV1 = static_cast<float>(uAtlasY + Bitmap.rows) / fAtlasHeight;
		}
		// A zero-size bitmap (a space) still gets an entry with an empty
		// rectangle, so the layout accounts for it without asking FreeType
		// about the same glyph again.

		++m_uAtlasVersion;
		return &m_GlyphEntries.Add(entry);
	}

	bool Font::ReserveAtlasRegion(uint32 uWidth, uint32 uHeight, uint32& outX, uint32& outY)
	{
		outX = 0;
		outY = 0;

		// A zero-size bitmap never reaches the atlas, and neither does one
		// asked for before the atlas exists.
		if (uWidth == 0 || uHeight == 0 || m_AtlasPixels.GetData() == nullptr)
		{
			return false;
		}

		const uint32 uRegionWidth = uWidth + k_nAtlasPadding;
		const uint32 uRegionHeight = uHeight + k_nAtlasPadding;

		// Shelf packer: the current row is full, so the next shelf starts at the
		// left edge, below the tallest glyph the previous shelf had to hold.
		if (m_nShelfX + uRegionWidth > m_nAtlasWidth)
		{
			m_nShelfY += m_nShelfHeight;
			m_nShelfX = 0;
			m_nShelfHeight = 0;
		}

		// A glyph wider than the atlas, or a shelf that no longer fits into its
		// height, means the atlas is full. That is worth exactly one error: the
		// text that needs the glyph is drawn without it, and reporting it for
		// every glyph of every run would bury the log.
		if (uRegionWidth > m_nAtlasWidth || m_nShelfY + uRegionHeight > m_nAtlasHeight)
		{
			if (!m_bAtlasOverflowReported)
			{
				m_bAtlasOverflowReported = true;
				LOG_ERROR(kLogTag, "The {}x{} glyph atlas is full; the glyphs that no longer fit stay empty.", m_nAtlasWidth, m_nAtlasHeight);
			}
			return false;
		}

		outX = m_nShelfX;
		outY = m_nShelfY;
		m_nShelfX += uRegionWidth;

		// The shelf is as tall as its tallest glyph, which is where the next
		// shelf starts.
		if (uRegionHeight > m_nShelfHeight)
		{
			m_nShelfHeight = uRegionHeight;
		}
		return true;
	}

	void Font::ResetAtlas()
	{
		m_nAtlasWidth = k_nDefaultAtlasSize;
		m_nAtlasHeight = k_nDefaultAtlasSize;

		const size_t nAtlasByteCount = static_cast<size_t>(m_nAtlasWidth) * static_cast<size_t>(m_nAtlasHeight) * 4u;

		// A reset atlas is fully transparent, because only the glyphs a run
		// actually needs are written into it. ArrayList has no bulk resize, so
		// the storage is reserved and cleared in one step: the geometry lives in
		// m_nAtlasWidth / m_nAtlasHeight and the pixels are handed out through
		// GetAtlasPixels().
		m_AtlasPixels.Clear();
		m_AtlasPixels.Reserve(nAtlasByteCount);
		if (m_AtlasPixels.GetData() == nullptr)
		{
			m_nAtlasWidth = 0;
			m_nAtlasHeight = 0;
			VSP_RETURN_VOID(kLogTag, "The {}x{} glyph atlas could not be allocated.", k_nDefaultAtlasSize, k_nDefaultAtlasSize);
		}
		memset(m_AtlasPixels.GetData(), 0, nAtlasByteCount);

		// The shelf packer starts at the top-left corner again.
		m_nShelfX = 0;
		m_nShelfY = 0;
		m_nShelfHeight = 0;

		// The cached entries address the atlas that just went away, and a fresh
		// atlas gets to overflow (and be reported) on its own.
		m_GlyphEntries.Clear();
		m_bAtlasOverflowReported = false;

		++m_uAtlasVersion;
	}
}
