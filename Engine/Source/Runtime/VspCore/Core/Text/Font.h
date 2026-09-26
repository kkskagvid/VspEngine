#pragma once

#include <vector>

#include "Core/Core.h"
#include "Core/String/VspString.h"
#include "Core/Templates/ArrayList.h"

namespace Vsp
{
	// -------------------------------------------------------------------------
	// TextGlyphQuad
	// -------------------------------------------------------------------------
	// One glyph of a laid-out text run, as a quad in TEXT PIXEL SPACE: the
	// origin is the pen position at the start of the first line, +X points right
	// and +Y points down - the coordinate system a UI layout works in.
	//
	// The atlas coordinates are normalized to [0, 1] and address the font's own
	// coverage atlas (Font::GetAtlasPixels).
	// -------------------------------------------------------------------------
	struct TextGlyphQuad
	{
		float fLeft = 0.0f;
		float fTop = 0.0f;
		float fRight = 0.0f;
		float fBottom = 0.0f;

		float fU0 = 0.0f;
		float fV0 = 0.0f;
		float fU1 = 0.0f;
		float fV1 = 0.0f;

		// Code point the glyph was shaped from (diagnostics and fallback).
		uint32 uCodePoint = 0;
	};

	// -------------------------------------------------------------------------
	// Font
	// -------------------------------------------------------------------------
	// A TrueType/OpenType face ready to be laid out and rasterized: the face
	// itself (FreeType maps code points to glyphs, reports the advances and the
	// 'kern' pairs, and rasterizes the bitmaps) and the coverage atlas those
	// bitmaps are packed into (RGBA8, white RGB with the coverage in the alpha
	// channel).
	//
	// Glyphs are rasterized on demand and packed into the atlas the first time
	// a text run needs them, so a font costs nothing until it is drawn with.
	// Whenever the atlas grows, GetAtlasVersion() changes and a caller that
	// uploaded the atlas to a texture knows it has to upload it again.
	//
	// Every function reports failure through its return value and logs the
	// reason; nothing throws.
	// -------------------------------------------------------------------------
#pragma warning(push)
#pragma warning(disable : 4251)   // ArrayList member: header-only template.
	class RUNTIME_API Font
	{
	public:
		// The atlas is a square coverage texture. 1024 x 1024 holds thousands of
		// glyphs at UI sizes, which is far more than one interface uses.
		static constexpr uint32 k_nDefaultAtlasSize = 1024;

		// Sizes below this are unreadable, so the value is clamped up.
		static constexpr float k_fMinimumPixelSize = 4.0f;
		static constexpr float k_fMaximumPixelSize = 256.0f;

		Font();
		~Font();

		// A font owns a FreeType face and its atlas, so it cannot be copied.
		Font(const Font&) = delete;
		Font& operator=(const Font&) = delete;

		// Moving hands the face and the atlas over and leaves the source
		// unloaded, which is what lets TextSystem keep its fonts in an
		// ArrayList (the array moves its elements when it grows).
		Font(Font&& Other) noexcept;
		Font& operator=(Font&& Other) noexcept;

		// -------- Loading --------
		// Reads a TTF/OTF file and prepares the face at the given pixel size.
		bool LoadFromFile(const VspString& sFilePathUtf8, float fPixelSize);

		// Prepares the face from a font already held in memory (the buffer must
		// stay alive for as long as the font is used).
		bool LoadFromMemory(const void* pFontData, size_t nByteCount, float fPixelSize);

		// Releases the face and the atlas. Safe to call on an unloaded font.
		void Unload();

		bool IsLoaded() const { return m_pFaceData != nullptr; }
		const VspString& GetSourcePath() const { return m_sSourcePath; }

		// -------- Metrics --------
		// The rasterization size in pixels (the face's EM size).
		void SetPixelSize(float fPixelSize);
		float GetPixelSize() const { return m_fPixelSize; }

		float GetAscent() const { return m_fAscent; }
		float GetDescent() const { return m_fDescent; }
		float GetLineHeight() const { return m_fLineHeight; }

		// -------- Measurement --------
		// Measures a text run in pixels. '\n' starts a new line.
		bool MeasureText(const VspString& sTextUtf8, float& outWidthPixels, float& outHeightPixels);

		// -------- Layout --------
		// Shapes a text run and writes one quad per glyph, in reading order.
		// Always returns the number of quads the run needs; when pOutQuads is
		// null, or uMaxQuadCount is smaller than that, only what fits is
		// written, so a caller can size its buffer with one call and fill it
		// with the next.
		uint32 LayoutText(const VspString& sTextUtf8, TextGlyphQuad* pOutQuads, uint32 uMaxQuadCount);

		// -------- Atlas --------
		uint32 GetAtlasWidth() const { return m_nAtlasWidth; }
		uint32 GetAtlasHeight() const { return m_nAtlasHeight; }

		// Tightly packed RGBA8 pixels: width * height * 4 bytes.
		const uint8* GetAtlasPixels() const { return m_AtlasPixels.GetData(); }

		// Changes whenever the atlas content did, so a caller knows when the
		// texture it uploaded is stale.
		uint32 GetAtlasVersion() const { return m_uAtlasVersion; }

	private:
		// One glyph bitmap already copied into the atlas.
		struct GlyphEntry
		{
			uint32 uGlyphIndex = 0;
			float fU0 = 0.0f;
			float fV0 = 0.0f;
			float fU1 = 0.0f;
			float fV1 = 0.0f;

			// Placement relative to the pen, in pixels: +X right, +Y down.
			float fBearingX = 0.0f;
			float fBearingY = 0.0f;
			float fWidth = 0.0f;
			float fHeight = 0.0f;
		};

		// Rasterizes one glyph and copies it into the atlas, or returns the
		// entry it already has.
		const GlyphEntry* GetGlyphEntry(uint32 uGlyphIndex);

		// Finds room in the atlas for a bitmap of the given size (shelf packer).
		// Returns false when the atlas is full.
		bool ReserveAtlasRegion(uint32 uWidth, uint32 uHeight, uint32& outX, uint32& outY);

		void ResetAtlas();
		void UpdateMetrics();

		void* m_pFaceData = nullptr;              // FT_Face, kept as void* so no
		                                          // FreeType header leaks here.

		VspString m_sSourcePath;

		// The font file's bytes when the face came from a file: FreeType
		// borrows the buffer it was created from, so the font owns it for as
		// long as the face lives.
		std::vector<uint8> m_FontFileBytes;

		float m_fPixelSize = 0.0f;
		float m_fAscent = 0.0f;
		float m_fDescent = 0.0f;
		float m_fLineHeight = 0.0f;

		ArrayList<GlyphEntry> m_GlyphEntries;

		uint32 m_nAtlasWidth = 0;
		uint32 m_nAtlasHeight = 0;
		ArrayList<uint8> m_AtlasPixels;

		// Shelf packer state: where the next glyph goes.
		uint32 m_nShelfX = 0;
		uint32 m_nShelfY = 0;
		uint32 m_nShelfHeight = 0;

		uint32 m_uAtlasVersion = 0;

		// Set when the atlas overflowed, so the failure is reported once
		// instead of once per glyph that no longer fits.
		bool m_bAtlasOverflowReported = false;
	};
#pragma warning(pop)
}
