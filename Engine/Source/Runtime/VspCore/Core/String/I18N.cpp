#include "RuntimePCH.h"

#include <cstring>
#include <string>

#include <unicode/locid.h>
#include <unicode/stringpiece.h>
#include <unicode/unistr.h>

#include "Common/PlatformMisc.h"
#include "Core/Diagnostics/ErrorHandling.h"
#include "Core/Json/JsonReader.h"
#include "Core/String/I18N.h"

namespace Vsp
{
	static constexpr const char* kLogTag = "I18N";

	// The tag a catalog without a "locale" member is filed under.
	static constexpr const char* k_pDefaultLocaleCode = "en-US";

	// The languages and scripts written right to left. The decision is made from
	// the locale's OWN subtags, which are part of the tag and therefore always
	// available: the ICU property data that would answer the same question for an
	// arbitrary code point is not linked into this build (see the header).
	static constexpr const char* k_pRightToLeftLanguages[] =
	{
		"ar",   // Arabic
		"he",   // Hebrew
		"fa",   // Persian
		"ur",   // Urdu
		"dv",   // Divehi
		"ps",   // Pashto
		"sd",   // Sindhi
		"ug",   // Uyghur
		"yi",   // Yiddish
		"ku",   // Kurdish (Sorani)
		"ckb",
	};

	static constexpr const char* k_pRightToLeftScripts[] =
	{
		"Arab", "Hebr", "Thaa", "Syrc", "Nkoo", "Samr", "Mand", "Adlm", "Rohg", "Yezi", "Phli", "Phlp",
	};

	I18N& I18N::Get()
	{
		static I18N s_Instance;
		return s_Instance;
	}

	// -------------------------------------------------------------------------
	// Conversion
	// -------------------------------------------------------------------------

	icu::UnicodeString I18N::FromUtf8(const VspString& sTextUtf8)
	{
		if (sTextUtf8.IsEmpty())
		{
			return icu::UnicodeString();
		}

		// The byte count is passed explicitly, so a string that carries an
		// embedded terminator converts the way it was stored. fromUTF8() replaces
		// malformed bytes with U+FFFD, so the result is always well formed.
		return icu::UnicodeString::fromUTF8(icu::StringPiece(
			sTextUtf8.GetData(), static_cast<int32_t>(sTextUtf8.GetByteLength())));
	}

	icu::UnicodeString I18N::FromUtf8(const char* pTextUtf8)
	{
		if (pTextUtf8 == nullptr)
		{
			return icu::UnicodeString();
		}
		return icu::UnicodeString::fromUTF8(icu::StringPiece(pTextUtf8, -1));
	}

	VspString I18N::ToUtf8(const icu::UnicodeString& Text)
	{
		if (Text.isEmpty())
		{
			return VspString();
		}

		std::string utf8Bytes;
		Text.toUTF8String(utf8Bytes);
		return VspString(utf8Bytes.data(), utf8Bytes.size());
	}

	// -------------------------------------------------------------------------
	// Locale
	// -------------------------------------------------------------------------

	void I18N::SetLocale(const icu::Locale& Locale)
	{
		m_Locale = Locale;
		LOG_INFO(kLogTag, "Locale is now '{}' (fallback '{}').",
			ToUtf8(GetLocaleCode()).GetData(), ToUtf8(GetFallbackLocaleCode()).GetData());
	}

	void I18N::SetLocale(const icu::UnicodeString& LocaleCode)
	{
		// ICU parses the tag itself: "zh-CN", "zh_CN" and "zh-Hans-CN" all land
		// on the same language and script without the engine splitting strings.
		SetLocale(icu::Locale(ToUtf8(LocaleCode).GetData()));
	}

	void I18N::SetLocale(const VspString& sLocaleCodeUtf8)
	{
		SetLocale(icu::Locale(sLocaleCodeUtf8.GetData()));
	}

	icu::UnicodeString I18N::GetLocaleCode() const
	{
		return icu::UnicodeString::fromUTF8(m_Locale.getName());
	}

	VspString I18N::GetLocaleCodeUtf8() const
	{
		return VspString(m_Locale.getName());
	}

	void I18N::SetFallbackLocale(const icu::Locale& Locale)
	{
		m_FallbackLocale = Locale;
	}

	void I18N::SetFallbackLocale(const icu::UnicodeString& LocaleCode)
	{
		SetFallbackLocale(icu::Locale(ToUtf8(LocaleCode).GetData()));
	}

	void I18N::SetFallbackLocale(const VspString& sLocaleCodeUtf8)
	{
		SetFallbackLocale(icu::Locale(sLocaleCodeUtf8.GetData()));
	}

	icu::UnicodeString I18N::GetFallbackLocaleCode() const
	{
		return icu::UnicodeString::fromUTF8(m_FallbackLocale.getName());
	}

	icu::Locale I18N::DetectSystemLocale()
	{
		// ICU answers this from the operating system itself, so the engine asks
		// one library what the machine is set to instead of guessing.
		return icu::Locale::getDefault();
	}

	VspString I18N::DetectSystemLocaleUtf8()
	{
		return ToUtf8(icu::UnicodeString::fromUTF8(DetectSystemLocale().getName()));
	}

	bool I18N::IsRightToLeftLocale(const icu::Locale& Locale)
	{
		const char* pLanguage = Locale.getLanguage();
		if (pLanguage != nullptr)
		{
			for (const char* pRightToLeftLanguage : k_pRightToLeftLanguages)
			{
				if (strcmp(pLanguage, pRightToLeftLanguage) == 0)
				{
					return true;
				}
			}
		}

		// A tag that names the script decides on its own, which is what makes
		// "ku-Latn" (left to right) and "ku-Arab" (right to left) differ.
		const char* pScript = Locale.getScript();
		if (pScript != nullptr && pScript[0] != '\0')
		{
			for (const char* pRightToLeftScript : k_pRightToLeftScripts)
			{
				if (strcmp(pScript, pRightToLeftScript) == 0)
				{
					return true;
				}
			}

			// The tag named a script and it is not one of them: the locale is
			// left to right whatever its language usually is.
			return false;
		}

		return false;
	}

	bool I18N::IsRightToLeft() const
	{
		return IsRightToLeftLocale(m_Locale);
	}

	// -------------------------------------------------------------------------
	// Catalog
	// -------------------------------------------------------------------------

	bool I18N::AddEntry(const icu::Locale& Locale, const icu::UnicodeString& Key, const icu::UnicodeString& Text)
	{
		if (Key.isEmpty())
		{
			VSP_RETURN_EMPTY(false, kLogTag, "AddEntry was given an empty key.");
		}

		// An existing entry is replaced, so a catalog loaded later can override
		// one loaded earlier.
		for (size_t nEntryIndex = 0; nEntryIndex < m_Entries.GetSize(); ++nEntryIndex)
		{
			Entry& entry = m_Entries[nEntryIndex];
			if (entry.Locale == Locale && entry.Key == Key)
			{
				entry.Text = Text;
				return true;
			}
		}

		Entry newEntry;
		newEntry.Locale = Locale;
		newEntry.Key = Key;
		newEntry.Text = Text;
		m_Entries.Add(newEntry);
		return true;
	}

	bool I18N::AddEntry(const VspString& sLocaleCodeUtf8, const VspString& sKeyUtf8, const VspString& sTextUtf8)
	{
		return AddEntry(icu::Locale(sLocaleCodeUtf8.GetData()), FromUtf8(sKeyUtf8), FromUtf8(sTextUtf8));
	}

	void I18N::ClearEntries()
	{
		m_Entries.Clear();
		m_ReportedMissingKeys.Clear();
	}

	uint32 I18N::GetEntryCountForLocale(const icu::Locale& Locale) const
	{
		uint32 uEntryCount = 0;
		for (size_t nEntryIndex = 0; nEntryIndex < m_Entries.GetSize(); ++nEntryIndex)
		{
			if (m_Entries[nEntryIndex].Locale == Locale)
			{
				++uEntryCount;
			}
		}
		return uEntryCount;
	}

	const I18N::Entry* I18N::FindEntry(const icu::Locale& Locale, const icu::UnicodeString& Key) const
	{
		for (size_t nEntryIndex = 0; nEntryIndex < m_Entries.GetSize(); ++nEntryIndex)
		{
			const Entry& entry = m_Entries[nEntryIndex];
			if (entry.Locale == Locale && entry.Key == Key)
			{
				return &entry;
			}
		}
		return nullptr;
	}

	// -------------------------------------------------------------------------
	// Lookup
	// -------------------------------------------------------------------------

	bool I18N::TryTranslate(const icu::UnicodeString& Key, icu::UnicodeString& outText) const
	{
		outText.remove();
		if (Key.isEmpty())
		{
			// Not worth an error entry: asking for nothing is a caller's no-op.
			return false;
		}

		if (const Entry* pEntry = FindEntry(m_Locale, Key))
		{
			outText = pEntry->Text;
			return true;
		}

		if (const Entry* pFallbackEntry = FindEntry(m_FallbackLocale, Key))
		{
			outText = pFallbackEntry->Text;
			return true;
		}

		return false;
	}

	bool I18N::TryTranslate(const VspString& sKeyUtf8, icu::UnicodeString& outText) const
	{
		return TryTranslate(FromUtf8(sKeyUtf8), outText);
	}

	icu::UnicodeString I18N::Translate(const icu::UnicodeString& Key) const
	{
		icu::UnicodeString Text;
		if (TryTranslate(Key, Text))
		{
			return Text;
		}

		// A missing translation shows the key itself: the UI stays readable and
		// the gap is obvious in a screenshot. It is reported at INFO level, not
		// as a failure - a half-translated catalog is a normal state of a game
		// in progress, and the key on screen is already the visible symptom.
		//
		// Each key is reported ONCE: a widget asks for its label every frame, so
		// repeating the message would bury the log that has to stay readable.
		ReportMissingKey(Key);
		return Key;
	}

	icu::UnicodeString I18N::Translate(const VspString& sKeyUtf8) const
	{
		return Translate(FromUtf8(sKeyUtf8));
	}

	VspString I18N::TranslateUtf8(const VspString& sKeyUtf8) const
	{
		return ToUtf8(Translate(sKeyUtf8));
	}

	void I18N::ReportMissingKey(const icu::UnicodeString& Key) const
	{
		for (size_t nKeyIndex = 0; nKeyIndex < m_ReportedMissingKeys.GetSize(); ++nKeyIndex)
		{
			if (m_ReportedMissingKeys[nKeyIndex] == Key)
			{
				return;   // Already said so.
			}
		}

		// The list is bounded: a catalog that is missing altogether would
		// otherwise grow it for every key a UI ever asks for.
		if (m_ReportedMissingKeys.GetSize() >= k_nMaximumMissingKeyReportCount)
		{
			return;
		}

		m_ReportedMissingKeys.Add(Key);
		LOG_INFO(kLogTag, "No entry for key '{}' in locale '{}'; the key is shown instead.",
			ToUtf8(Key).GetData(), GetLocaleCodeUtf8().GetData());
	}

	// -------------------------------------------------------------------------
	// Loading
	// -------------------------------------------------------------------------

	// The path of the catalog file for one locale: ICU's canonical name first
	// ("zh_CN.json"), then the same name with dashes ("zh-CN.json"). Empty when
	// neither exists.
	VspString I18N::FindCatalogFilePath(const VspString& sDirectoryPathUtf8, const icu::Locale& Locale)
	{
		const VspString sCanonicalCode(Locale.getName());
		VspString sDashedCode(sCanonicalCode);
		for (size_t nByteIndex = 0; nByteIndex < sDashedCode.GetByteLength(); ++nByteIndex)
		{
			if (sDashedCode.GetData()[nByteIndex] == '_')
			{
				// VspString is immutable through GetData(), so the dashed form is
				// built by copying the pieces around every separator.
				sDashedCode = sDashedCode.GetSubString(0, nByteIndex) + "-"
					+ sDashedCode.GetSubString(nByteIndex + 1, sDashedCode.GetByteLength() - nByteIndex - 1);
			}
		}

		const VspString sCandidates[2] =
		{
			sDirectoryPathUtf8 + "\\" + sCanonicalCode + ".json",
			sDirectoryPathUtf8 + "\\" + sDashedCode + ".json",
		};

		for (const VspString& sCandidatePath : sCandidates)
		{
			if (PlatformMisc::DoesFileExist(sCandidatePath))
			{
				return sCandidatePath;
			}
		}
		return VspString();
	}

	VspString I18N::GetDefaultCatalogDirectoryPath()
	{
		return PlatformMisc::GetExecutableDirectoryPath() + "\\Locales";
	}

	bool I18N::LoadCatalogFile(const VspString& sFilePathUtf8)
	{
		JsonValue root;
		VspString sParseError;
		if (!JsonReader::ParseFile(sFilePathUtf8, root, sParseError))
		{
			VSP_RETURN_EMPTY(false, kLogTag, "The catalog '{}' could not be read: {}",
				sFilePathUtf8.GetData(), sParseError.GetData());
		}

		if (!root.IsObject())
		{
			VSP_RETURN_EMPTY(false, kLogTag, "The catalog '{}' is not a JSON object.", sFilePathUtf8.GetData());
		}

		// The locale comes from the "locale" member; a file that does not name
		// one is filed under the current locale, which is the useful default for
		// a hand-written catalog. ICU parses whatever the file says.
		const VspString sDeclaredLocaleCode = root.GetMemberString("locale", "");
		icu::Locale catalogLocale(sDeclaredLocaleCode.GetData());
		if (sDeclaredLocaleCode.IsEmpty() || catalogLocale.isBogus())
		{
			catalogLocale = m_Locale;
			LOG_INFO(kLogTag, "The catalog '{}' names no locale; it is filed under '{}'.",
				sFilePathUtf8.GetData(), catalogLocale.getName());
		}

		const JsonValue& entries = root["entries"];
		if (!entries.IsObject())
		{
			VSP_RETURN_EMPTY(false, kLogTag, "The catalog '{}' has no 'entries' object.", sFilePathUtf8.GetData());
		}

		uint32 uLoadedCount = 0;
		for (uint32 uMemberIndex = 0; uMemberIndex < entries.GetMemberCount(); ++uMemberIndex)
		{
			const VspString& sKey = entries.GetMemberName(uMemberIndex);
			const JsonValue& textValue = entries.GetMember(uMemberIndex);
			if (!textValue.IsValid())
			{
				continue;
			}

			// A non-string value is stringified rather than dropped, so a catalog
			// may hold numbers ("ui.limit": 10) without a second syntax.
			VspString sTextUtf8;
			if (textValue.GetType() == JsonValue::Type::String)
			{
				sTextUtf8 = textValue.GetString();
			}
			else if (textValue.GetType() == JsonValue::Type::Number)
			{
				sTextUtf8 = VspFormat::Format("{}", static_cast<float>(textValue.GetNumber()));
			}
			else if (textValue.GetType() == JsonValue::Type::Boolean)
			{
				sTextUtf8 = textValue.GetBoolean() ? VspString("true") : VspString("false");
			}
			else
			{
				continue;
			}

			if (AddEntry(catalogLocale, FromUtf8(sKey), FromUtf8(sTextUtf8)))
			{
				++uLoadedCount;
			}
		}

		LOG_INFO(kLogTag, "Catalog '{}' loaded: {} entry(ies) for locale '{}'.",
			sFilePathUtf8.GetData(), uLoadedCount, catalogLocale.getName());
		return true;
	}

	bool I18N::LoadCatalogDirectory(const VspString& sDirectoryPathUtf8)
	{
		if (sDirectoryPathUtf8.IsEmpty())
		{
			VSP_RETURN_EMPTY(false, kLogTag, "LoadCatalogDirectory was given an empty directory.");
		}

		// Both the current and the fallback locale are read: the fallback is
		// what fills the gaps of a partial translation.
		bool bLoadedAnyCatalog = false;
		const icu::Locale localesToLoad[2] = { m_Locale, m_FallbackLocale };
		for (const icu::Locale& locale : localesToLoad)
		{
			if (locale.isBogus())
			{
				continue;
			}

			// ICU spells a locale canonically ("zh_CN"), while a catalog file is
			// usually named the way the tag is written ("zh-CN"). Both spellings
			// are looked for, so a translator names the file whichever way they
			// think of the locale.
			const VspString sFilePath = FindCatalogFilePath(sDirectoryPathUtf8, locale);
			if (sFilePath.IsEmpty())
			{
				// A missing catalog is a normal state, not a failure: the other
				// locale and the keys themselves carry the UI.
				LOG_INFO(kLogTag, "No catalog for locale '{}' in '{}'.", locale.getName(), sDirectoryPathUtf8.GetData());
				continue;
			}

			bLoadedAnyCatalog |= LoadCatalogFile(sFilePath);
		}

		return bLoadedAnyCatalog;
	}
}
