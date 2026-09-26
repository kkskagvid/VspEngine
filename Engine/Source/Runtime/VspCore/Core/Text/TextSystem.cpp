#include "RuntimePCH.h"

#include <algorithm>

#include "Common/PlatformMisc.h"
#include "Core/Diagnostics/ErrorHandling.h"
#include "Core/Text/TextSystem.h"

#if VSP_PLATFORM_WINDOWS
	#define WIN32_LEAN_AND_MEAN
	#include <Windows.h>

	// Windows.h maps the GDI font functions onto their W/A variants with macros,
	// and CreateFontW would rename TextSystem::CreateFont with it.
	#undef CreateFont
#endif

// -------------------------------------------------------------------------
// TextSystem
// -------------------------------------------------------------------------
// The engine's text service: it owns the loaded fonts, hands one stable,
// 1-based handle to each of them and decides which font an interface starts
// with.
//
// Design decisions worth knowing:
//   * Handles are 1-based and never reused (0 always means "invalid"), while
//     the slots holding the fonts are recycled: a released slot is marked dead
//     and the next font takes it. The handle lives inside its slot, so FindFont
//     validates a handle by comparing it and never trusts a slot position.
//   * CreateDefaultFont probes the font files shipped next to the executable
//     first - alphabetically, so the choice is reproducible - and the
//     well-known Windows system fonts afterwards.
//   * Finding no font at all is not something the engine can repair, so it is
//     logged and reported as handle 0; a UI has to cope without text. Nothing
//     here throws.
// -------------------------------------------------------------------------

namespace Vsp
{
	static constexpr const char* kLogTag = "TextSystem";

#if VSP_PLATFORM_WINDOWS
	static constexpr const char* k_pPathSeparator = "\\";
#else
	static constexpr const char* k_pPathSeparator = "/";
#endif

	// The system fonts at least one of which every supported Windows version
	// ships, probed in this order.
	//
	// The order matters more than it looks: a font is chosen once for the whole
	// interface, and a face that has no glyph for a character draws the .notdef
	// box for it. The CJK-capable faces are therefore probed FIRST, because they
	// also carry the full Latin set, while the Latin-only faces (Segoe UI, Arial)
	// would turn every Chinese, Japanese or Korean string into boxes. On a machine
	// that ships none of the CJK faces the probe simply falls through to them.
	static constexpr const char* k_pSystemFontFileNames[] =
	{
		"msyh.ttc",      // Microsoft YaHei, Simplified Chinese + Latin
		"msyh.ttf",
		"simhei.ttf",    // SimHei, Simplified Chinese
		"simsun.ttc",    // SimSun, Simplified Chinese
		"meiryo.ttc",    // Meiryo, Japanese
		"malgun.ttf",    // Malgun Gothic, Korean
		"segoeui.ttf",   // Segoe UI, Latin
		"arial.ttf",
		"seguisym.ttf",
	};

	namespace
	{
		// Compares two font file paths ordinally, which for file names of the
		// same directory is plain alphabetical order.
		bool CompareFontFilePaths(const VspString& sLeftPath, const VspString& sRightPath)
		{
			return sLeftPath.CompareTo(sRightPath) < 0;
		}

#if VSP_PLATFORM_WINDOWS
		// Lowercases one ASCII byte; a font file name is ASCII in practice, and
		// anything else simply does not match an extension.
		char ToLowerAscii(char cCharacter)
		{
			return (cCharacter >= 'A' && cCharacter <= 'Z') ? static_cast<char>(cCharacter - 'A' + 'a') : cCharacter;
		}

		// True when the path ends in ".ttf" or ".otf", in any case: a shipped
		// file may well be called "MyFont.TTF".
		bool HasFontFileExtension(const VspString& sFilePath)
		{
			const size_t nByteCount = sFilePath.GetByteLength();
			if (nByteCount < 4 || sFilePath.GetData()[nByteCount - 4] != '.')
			{
				return false;
			}

			const char* pExtension = sFilePath.GetData() + (nByteCount - 3);
			const bool bIsTrueType = ToLowerAscii(pExtension[0]) == 't' && ToLowerAscii(pExtension[1]) == 't' && ToLowerAscii(pExtension[2]) == 'f';
			const bool bIsOpenType = ToLowerAscii(pExtension[0]) == 'o' && ToLowerAscii(pExtension[1]) == 't' && ToLowerAscii(pExtension[2]) == 'f';
			return bIsTrueType || bIsOpenType;
		}

		// Collects every *.ttf / *.otf file of a directory, in no particular
		// order (the caller sorts them). A directory that does not exist - the
		// usual case, because a game only ships Fonts when it wants its own
		// typeface - simply collects nothing.
		void CollectFontFilePaths(const VspString& sDirectoryPath, ArrayList<VspString>& outFontFilePaths)
		{
			const VspString sSearchPatternText = sDirectoryPath + k_pPathSeparator + "*";
			const ArrayList<wchar_t> sSearchPattern = sSearchPatternText.ToWideText();

			WIN32_FIND_DATAW findData = {};
			HANDLE hFindFile = ::FindFirstFileW(sSearchPattern.GetData(), &findData);
			if (hFindFile == INVALID_HANDLE_VALUE)
			{
				return;
			}

			do
			{
				if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
				{
					continue;
				}

				const VspString sFontFilePath = sDirectoryPath + k_pPathSeparator + VspString(findData.cFileName);
				if (HasFontFileExtension(sFontFilePath))
				{
					outFontFilePaths.Add(sFontFilePath);
				}
			} while (::FindNextFileW(hFindFile, &findData) != 0);

			::FindClose(hFindFile);
		}
#endif
	}

	TextSystem& TextSystem::Get()
	{
		static TextSystem s_Instance;
		return s_Instance;
	}

	// -------------------------------------------------------------------------
	// Creation
	// -------------------------------------------------------------------------

	uint32 TextSystem::CreateFont(const VspString& sFilePathUtf8, float fPixelSize)
	{
		// A released slot is reused before the table grows: a UI creates and
		// destroys fonts as its screens come and go.
		FontEntry* pEntry = nullptr;
		for (size_t nSlotIndex = 0; nSlotIndex < m_Fonts.GetSize(); ++nSlotIndex)
		{
			if (!m_Fonts[nSlotIndex].bIsAlive)
			{
				pEntry = &m_Fonts[nSlotIndex];
				break;
			}
		}
		if (pEntry == nullptr)
		{
			pEntry = &m_Fonts.Emplace();
		}

		if (!pEntry->Face.LoadFromFile(sFilePathUtf8, fPixelSize))
		{
			// LoadFromFile has logged the reason. The slot stays released, so
			// the handle a caller never received cannot address a dead font.
			pEntry->Face.Unload();
			pEntry->uHandle = 0;
			pEntry->bIsAlive = false;
			return 0;
		}

		// Handles only ever go up, so a handle whose font was destroyed never
		// resolves again - it is not handed to a later font either.
		pEntry->uHandle = m_uNextHandle++;
		pEntry->bIsAlive = true;

		LOG_DEBUG(kLogTag, "Font '{}' was created with handle {} at {} px.",
			pEntry->Face.GetSourcePath().GetData(), pEntry->uHandle, pEntry->Face.GetPixelSize());
		return pEntry->uHandle;
	}

	uint32 TextSystem::CreateFontFromMemory(const void* pFontData, size_t nByteCount, float fPixelSize)
	{
		FontEntry* pEntry = nullptr;
		for (size_t nSlotIndex = 0; nSlotIndex < m_Fonts.GetSize(); ++nSlotIndex)
		{
			if (!m_Fonts[nSlotIndex].bIsAlive)
			{
				pEntry = &m_Fonts[nSlotIndex];
				break;
			}
		}
		if (pEntry == nullptr)
		{
			pEntry = &m_Fonts.Emplace();
		}

		if (!pEntry->Face.LoadFromMemory(pFontData, nByteCount, fPixelSize))
		{
			// LoadFromMemory has logged the reason; the slot stays released.
			pEntry->Face.Unload();
			pEntry->uHandle = 0;
			pEntry->bIsAlive = false;
			return 0;
		}

		pEntry->uHandle = m_uNextHandle++;
		pEntry->bIsAlive = true;

		LOG_DEBUG(kLogTag, "A font of {} byte(s) was created with handle {} at {} px.",
			nByteCount, pEntry->uHandle, pEntry->Face.GetPixelSize());
		return pEntry->uHandle;
	}

	uint32 TextSystem::CreateDefaultFont(float fPixelSize)
	{
		// 1. A font shipped next to the executable wins: it is the typeface the
		//    game picked for its interface.
		const VspString sFontDirectory = PlatformMisc::GetExecutableDirectoryPath() + k_pPathSeparator + k_pDefaultFontDirectoryName;

		// Every candidate is collected before one is tried, because the file
		// system hands directory entries out in no particular order and "the
		// first font in the Fonts directory" has to mean the same file on every
		// run.
		ArrayList<VspString> fontFilePaths;
#if VSP_PLATFORM_WINDOWS
		CollectFontFilePaths(sFontDirectory, fontFilePaths);
#endif
		if (fontFilePaths.GetSize() > 1)
		{
			std::sort(fontFilePaths.GetData(), fontFilePaths.GetData() + fontFilePaths.GetSize(), CompareFontFilePaths);
		}

		for (size_t nFontIndex = 0; nFontIndex < fontFilePaths.GetSize(); ++nFontIndex)
		{
			const uint32 uFontHandle = CreateFont(fontFilePaths[nFontIndex], fPixelSize);
			if (uFontHandle != 0)
			{
				LOG_INFO(kLogTag, "The default font is '{}'.", fontFilePaths[nFontIndex].GetData());
				return uFontHandle;
			}
		}

		// 2. Otherwise the well-known system fonts, probed in a fixed order so
		//    the choice does not depend on the machine either.
#if VSP_PLATFORM_WINDOWS
		char sWindowsDirectoryBuffer[MAX_PATH] = {};
		if (PlatformMisc::GetEnvironmentVariableValue("WINDIR", sWindowsDirectoryBuffer, MAX_PATH) > 0)
		{
			const VspString sSystemFontDirectory = VspString(sWindowsDirectoryBuffer) + k_pPathSeparator + "Fonts";
			for (const char* pFontFileName : k_pSystemFontFileNames)
			{
				const VspString sFontFilePath = sSystemFontDirectory + k_pPathSeparator + pFontFileName;
				if (!PlatformMisc::DoesFileExist(sFontFilePath))
				{
					continue;
				}

				const uint32 uFontHandle = CreateFont(sFontFilePath, fPixelSize);
				if (uFontHandle != 0)
				{
					LOG_INFO(kLogTag, "The default font is '{}'.", sFontFilePath.GetData());
					return uFontHandle;
				}
			}
		}
#endif

		// Not fatal: an interface without text still runs, so the caller decides
		// what to do about it.
		LOG_INFO(kLogTag, "No font was found in '{}' or in the system font directory.", sFontDirectory.GetData());
		return 0;
	}

	// -------------------------------------------------------------------------
	// Lookup and release
	// -------------------------------------------------------------------------

	bool TextSystem::DestroyFont(uint32 uFontHandle)
	{
		if (uFontHandle == 0)
		{
			VSP_RETURN_EMPTY(false, kLogTag, "Font handle 0 does not address a live font.");
		}

		for (size_t nSlotIndex = 0; nSlotIndex < m_Fonts.GetSize(); ++nSlotIndex)
		{
			FontEntry& Entry = m_Fonts[nSlotIndex];
			if (Entry.bIsAlive && Entry.uHandle == uFontHandle)
			{
				Entry.Face.Unload();

				// The slot is free again, but the handle is retired: it must not
				// address whatever font takes the slot next.
				Entry.uHandle = 0;
				Entry.bIsAlive = false;
				return true;
			}
		}

		VSP_RETURN_EMPTY(false, kLogTag, "Font handle {} does not address a live font.", uFontHandle);
	}

	Font* TextSystem::FindFont(uint32 uFontHandle)
	{
		if (uFontHandle == 0)
		{
			return nullptr;
		}

		for (size_t nSlotIndex = 0; nSlotIndex < m_Fonts.GetSize(); ++nSlotIndex)
		{
			FontEntry& Entry = m_Fonts[nSlotIndex];
			if (Entry.bIsAlive && Entry.uHandle == uFontHandle)
			{
				return &Entry.Face;
			}
		}
		return nullptr;
	}

	const Font* TextSystem::FindFont(uint32 uFontHandle) const
	{
		if (uFontHandle == 0)
		{
			return nullptr;
		}

		for (size_t nSlotIndex = 0; nSlotIndex < m_Fonts.GetSize(); ++nSlotIndex)
		{
			const FontEntry& Entry = m_Fonts[nSlotIndex];
			if (Entry.bIsAlive && Entry.uHandle == uFontHandle)
			{
				return &Entry.Face;
			}
		}
		return nullptr;
	}

	uint32 TextSystem::GetLiveFontCount() const
	{
		uint32 nLiveFontCount = 0;
		for (size_t nSlotIndex = 0; nSlotIndex < m_Fonts.GetSize(); ++nSlotIndex)
		{
			if (m_Fonts[nSlotIndex].bIsAlive)
			{
				++nLiveFontCount;
			}
		}
		return nLiveFontCount;
	}

	void TextSystem::Clear()
	{
		for (size_t nSlotIndex = 0; nSlotIndex < m_Fonts.GetSize(); ++nSlotIndex)
		{
			m_Fonts[nSlotIndex].Face.Unload();
			m_Fonts[nSlotIndex].uHandle = 0;
			m_Fonts[nSlotIndex].bIsAlive = false;
		}

		// The table itself is kept: its slots are reused, and the handles that
		// were already handed out keep addressing nothing.
	}
}
