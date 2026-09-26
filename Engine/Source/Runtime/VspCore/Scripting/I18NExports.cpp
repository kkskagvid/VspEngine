#include "RuntimePCH.h"

#include <cstring>

#include <unicode/stringpiece.h>
#include <unicode/unistr.h>

#include "Core/Diagnostics/ErrorHandling.h"
#include "Core/String/I18N.h"
#include "Scripting/ScriptExport.h"

// -------------------------------------------------------------------------
// Localisation exports consumed by managed code (C# -> C++ direction).
// VspEngine.I18N P/Invokes these exact names from VspCore.dll.
//
// Text handling belongs to the native Core/String/I18N service, which holds its
// text as ICU UnicodeString objects: a key is resolved, the fallback locale is
// consulted and the key itself comes back when nothing matches, so the managed
// side never has to reimplement any of it.
//
// A translation can leave in either encoding:
//   * UTF-8  - the engine's file/log/ABI encoding, and what the locale codes use;
//   * UTF-16 - the encoding a UnicodeString already holds, and the one the .NET
//              String type is made of, so the managed side gets the text with no
//              conversion at all.
//
// Every function that reports text fills a CALLER-OWNED buffer and returns the
// number of code units written (excluding the terminator). A missing buffer, an
// unknown locale or a null key produces the empty result - 0 - and a log entry,
// never a crash.
// -------------------------------------------------------------------------

static constexpr const char* kLogTag = "I18NExports";

namespace
{
	// Copies a UnicodeString into a caller-owned UTF-8 buffer, always
	// terminating it, and returns the byte count without the terminator.
	int32 CopyTextToUtf8Buffer(const icu::UnicodeString& Text, char* pBufferUtf8, int32 nBufferCapacityBytes)
	{
		if (pBufferUtf8 == nullptr || nBufferCapacityBytes <= 0)
		{
			VSP_LOG_ERROR(kLogTag, "A UTF-8 buffer of {} byte(s) cannot receive the result.", nBufferCapacityBytes);
			return 0;
		}

		// The engine's UTF-8 type does the transcoding, so there is exactly one
		// place where the two encodings meet.
		const Vsp::VspString sTextUtf8 = Vsp::I18N::ToUtf8(Text);
		const size_t nTextByteCount = sTextUtf8.GetByteLength();
		const size_t nMaximumCopyByteCount = static_cast<size_t>(nBufferCapacityBytes) - 1u;
		const size_t nCopyByteCount = (nTextByteCount < nMaximumCopyByteCount) ? nTextByteCount : nMaximumCopyByteCount;

		if (nCopyByteCount > 0)
		{
			memcpy(pBufferUtf8, sTextUtf8.GetData(), nCopyByteCount);
		}
		pBufferUtf8[nCopyByteCount] = '\0';
		return static_cast<int32>(nCopyByteCount);
	}

	// Copies a UnicodeString into a caller-owned UTF-16 buffer, always
	// terminating it, and returns the number of code units without the
	// terminator. The buffer holds UTF-16 CODE UNITS, not code points, so a
	// character outside the basic plane takes two of them - which is exactly what
	// a .NET String is made of.
	int32 CopyTextToUtf16Buffer(const icu::UnicodeString& Text, char16_t* pBufferUtf16, int32 nBufferCapacityUnits)
	{
		if (pBufferUtf16 == nullptr || nBufferCapacityUnits <= 0)
		{
			VSP_LOG_ERROR(kLogTag, "A UTF-16 buffer of {} unit(s) cannot receive the result.", nBufferCapacityUnits);
			return 0;
		}

		const int32 nTextUnitCount = Text.length();
		const int32 nMaximumCopyUnitCount = nBufferCapacityUnits - 1;
		const int32 nCopyUnitCount = (nTextUnitCount < nMaximumCopyUnitCount) ? nTextUnitCount : nMaximumCopyUnitCount;

		if (nCopyUnitCount > 0)
		{
			// getBuffer() hands out the UnicodeString's own code units, so this
			// is a plain copy of what the service already holds.
			memcpy(pBufferUtf16, Text.getBuffer(), static_cast<size_t>(nCopyUnitCount) * sizeof(char16_t));
		}
		pBufferUtf16[nCopyUnitCount] = u'\0';
		return nCopyUnitCount;
	}
}

// -------- Locale --------

CSHARP_EXPORT void VspI18N_SetLocale(const char* pLocaleCodeUtf8)
{
	Vsp::I18N::Get().SetLocale(icu::Locale(pLocaleCodeUtf8 != nullptr ? pLocaleCodeUtf8 : ""));
}

CSHARP_EXPORT int32 VspI18N_GetLocale(char* pBufferUtf8, int32 nBufferCapacityBytes)
{
	return CopyTextToUtf8Buffer(Vsp::I18N::Get().GetLocaleCode(), pBufferUtf8, nBufferCapacityBytes);
}

CSHARP_EXPORT void VspI18N_SetFallbackLocale(const char* pLocaleCodeUtf8)
{
	Vsp::I18N::Get().SetFallbackLocale(icu::Locale(pLocaleCodeUtf8 != nullptr ? pLocaleCodeUtf8 : ""));
}

CSHARP_EXPORT int32 VspI18N_GetFallbackLocale(char* pBufferUtf8, int32 nBufferCapacityBytes)
{
	return CopyTextToUtf8Buffer(Vsp::I18N::Get().GetFallbackLocaleCode(), pBufferUtf8, nBufferCapacityBytes);
}

CSHARP_EXPORT int32 VspI18N_DetectSystemLocale(char* pBufferUtf8, int32 nBufferCapacityBytes)
{
	return CopyTextToUtf8Buffer(
		icu::UnicodeString::fromUTF8(Vsp::I18N::DetectSystemLocale().getName()), pBufferUtf8, nBufferCapacityBytes);
}

CSHARP_EXPORT int32 VspI18N_IsRightToLeft()
{
	return Vsp::I18N::Get().IsRightToLeft() ? 1 : 0;
}

CSHARP_EXPORT int32 VspI18N_IsRightToLeftLocale(const char* pLocaleCodeUtf8)
{
	return Vsp::I18N::IsRightToLeftLocale(icu::Locale(pLocaleCodeUtf8 != nullptr ? pLocaleCodeUtf8 : "")) ? 1 : 0;
}

// -------- Catalog --------

CSHARP_EXPORT int32 VspI18N_AddEntry(const char* pLocaleCodeUtf8, const char* pKeyUtf8, const char* pTextUtf8)
{
	return Vsp::I18N::Get().AddEntry(
		Vsp::VspString(pLocaleCodeUtf8), Vsp::VspString(pKeyUtf8), Vsp::VspString(pTextUtf8)) ? 1 : 0;
}

CSHARP_EXPORT int32 VspI18N_GetEntryCount()
{
	return static_cast<int32>(Vsp::I18N::Get().GetEntryCount());
}

CSHARP_EXPORT void VspI18N_ClearEntries()
{
	Vsp::I18N::Get().ClearEntries();
}

// -------- Lookup --------

CSHARP_EXPORT int32 VspI18N_TryTranslate(const char* pKeyUtf8, char* pBufferUtf8, int32 nBufferCapacityBytes)
{
	if (pKeyUtf8 == nullptr)
	{
		VSP_LOG_ERROR(kLogTag, "VspI18N_TryTranslate was given a null key.");
		return 0;
	}

	icu::UnicodeString sText;
	if (!Vsp::I18N::Get().TryTranslate(icu::UnicodeString::fromUTF8(pKeyUtf8), sText))
	{
		// Nothing matched anywhere: the empty result, as the interface says.
		if (pBufferUtf8 != nullptr && nBufferCapacityBytes > 0)
		{
			pBufferUtf8[0] = '\0';
		}
		return 0;
	}

	return CopyTextToUtf8Buffer(sText, pBufferUtf8, nBufferCapacityBytes);
}

CSHARP_EXPORT int32 VspI18N_Translate(const char* pKeyUtf8, char* pBufferUtf8, int32 nBufferCapacityBytes)
{
	if (pKeyUtf8 == nullptr)
	{
		VSP_LOG_ERROR(kLogTag, "VspI18N_Translate was given a null key.");
		return 0;
	}

	// Translate() never returns nothing: a missing key comes back as the key
	// itself, which keeps a UI readable while a catalog is still incomplete.
	return CopyTextToUtf8Buffer(
		Vsp::I18N::Get().Translate(icu::UnicodeString::fromUTF8(pKeyUtf8)), pBufferUtf8, nBufferCapacityBytes);
}

// The same lookups, in the encoding a UnicodeString already holds and .NET
// String is made of.

CSHARP_EXPORT int32 VspI18N_TryTranslateUtf16(const char* pKeyUtf8, char16_t* pBufferUtf16, int32 nBufferCapacityUnits)
{
	if (pKeyUtf8 == nullptr)
	{
		VSP_LOG_ERROR(kLogTag, "VspI18N_TryTranslateUtf16 was given a null key.");
		return 0;
	}

	icu::UnicodeString sText;
	if (!Vsp::I18N::Get().TryTranslate(icu::UnicodeString::fromUTF8(pKeyUtf8), sText))
	{
		if (pBufferUtf16 != nullptr && nBufferCapacityUnits > 0)
		{
			pBufferUtf16[0] = u'\0';
		}
		return 0;
	}

	return CopyTextToUtf16Buffer(sText, pBufferUtf16, nBufferCapacityUnits);
}

CSHARP_EXPORT int32 VspI18N_TranslateUtf16(const char* pKeyUtf8, char16_t* pBufferUtf16, int32 nBufferCapacityUnits)
{
	if (pKeyUtf8 == nullptr)
	{
		VSP_LOG_ERROR(kLogTag, "VspI18N_TranslateUtf16 was given a null key.");
		return 0;
	}

	return CopyTextToUtf16Buffer(
		Vsp::I18N::Get().Translate(icu::UnicodeString::fromUTF8(pKeyUtf8)), pBufferUtf16, nBufferCapacityUnits);
}

// -------- Loading --------

CSHARP_EXPORT int32 VspI18N_LoadCatalogFile(const char* pFilePathUtf8)
{
	if (pFilePathUtf8 == nullptr)
	{
		VSP_LOG_ERROR(kLogTag, "VspI18N_LoadCatalogFile was given a null path.");
		return 0;
	}
	return Vsp::I18N::Get().LoadCatalogFile(Vsp::VspString(pFilePathUtf8)) ? 1 : 0;
}

CSHARP_EXPORT int32 VspI18N_LoadCatalogDirectory(const char* pDirectoryPathUtf8)
{
	if (pDirectoryPathUtf8 == nullptr)
	{
		VSP_LOG_ERROR(kLogTag, "VspI18N_LoadCatalogDirectory was given a null path.");
		return 0;
	}
	return Vsp::I18N::Get().LoadCatalogDirectory(Vsp::VspString(pDirectoryPathUtf8)) ? 1 : 0;
}

CSHARP_EXPORT int32 VspI18N_GetDefaultCatalogDirectory(char* pBufferUtf8, int32 nBufferCapacityBytes)
{
	return CopyTextToUtf8Buffer(
		icu::UnicodeString::fromUTF8(Vsp::I18N::GetDefaultCatalogDirectoryPath().GetData()),
		pBufferUtf8,
		nBufferCapacityBytes);
}
