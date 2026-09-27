#pragma once

#include "Core/Core.h"
#include "Core/String/VspString.h"
#include "Core/Templates/ArrayList.h"
#include "Core/Text/Font.h"

namespace Vsp
{
	// -------------------------------------------------------------------------
	// TextSystem
	// -------------------------------------------------------------------------
	// The engine's text service: it owns the loaded fonts and hands managed code
	// a stable, 1-based handle for each of them, the same scheme the scene uses
	// for its objects (0 always means "invalid").
	//
	// Text is the one thing a UI cannot avoid, and a font is a real resource
	// (a face, a shaper and a coverage atlas), so the engine keeps them rather
	// than the game: a game names a font file once, gets a handle, and asks the
	// handle to measure and lay out its strings.
	//
	// Nothing here throws; a function that cannot do its work reports the empty
	// value - an invalid handle, an empty measurement - after logging why.
	// -------------------------------------------------------------------------
#pragma warning(push)
#pragma warning(disable : 4251)   // ArrayList member.
	class RUNTIME_API TextSystem
	{
	public:
		// Font files are looked up here first, next to the executable.
		static constexpr const char* k_pDefaultFontDirectoryName = "Fonts";

		static TextSystem& Get();

		// Loads one font file and prepares it at the given pixel size. Returns
		// the font handle (0 on failure).
		uint32 CreateFont(const VspString& sFilePathUtf8, float fPixelSize);

		// Loads a font from memory (the buffer must outlive the font). Returns
		// the font handle (0 on failure).
		uint32 CreateFontFromMemory(const void* pFontData, size_t nByteCount, float fPixelSize);

		// Loads the interface font: the first font file found in
		// "<executable directory>/Fonts", and when that directory holds none,
		// the first well-known system font that exists. Returns the font handle
		// (0 when no font could be loaded at all, which a UI must handle).
		uint32 CreateDefaultFont(float fPixelSize);

		// Releases one font. Returns false when the handle does not address a
		// live font.
		bool DestroyFont(uint32 uFontHandle);

		// The font a handle addresses, or nullptr.
		Font* FindFont(uint32 uFontHandle);
		const Font* FindFont(uint32 uFontHandle) const;

		uint32 GetLiveFontCount() const;

		// Releases every font (engine shutdown).
		void Clear();

	private:
		// The registry in Core/EngineServices.h owns this service's storage and
		// lifetime, so it has to be able to construct it.
		friend class EngineServices;

		TextSystem() = default;

		// The service is a process-wide singleton and a Font owns a face, so
		// neither copying nor assigning one is meaningful (the copied font
		// table would address the same faces twice).
		TextSystem(const TextSystem&) = delete;
		TextSystem& operator=(const TextSystem&) = delete;

		// One live font plus the handle that addresses it.
		struct FontEntry
		{
			Font Face;
			uint32 uHandle = 0;
			bool bIsAlive = false;
		};

		// Fonts kept in memory for the lifetime of the process: a face created
		// from a file owns its bytes, one created from memory borrows them.
		ArrayList<FontEntry> m_Fonts;
		uint32 m_uNextHandle = 1;
	};
#pragma warning(pop)
}
