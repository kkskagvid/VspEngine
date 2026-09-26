#pragma once

#include <unicode/locid.h>
#include <unicode/unistr.h>

#include "Core/Core.h"
#include "Core/String/VspString.h"
#include "Core/String/VspStringFormat.h"
#include "Core/Templates/ArrayList.h"

namespace Vsp
{
	// -------------------------------------------------------------------------
	// I18N
	// -------------------------------------------------------------------------
	// The engine's text localisation service. Everything user-visible the engine
	// and a game put on screen goes through it.
	//
	// TEXT IS AN ICU UnicodeString. That is what this service is FOR: a
	// UnicodeString is a length-counted UTF-16 string that already knows the
	// Unicode rules the engine would otherwise have to write itself - code point
	// access that never splits a surrogate pair, substring, search and comparison
	// in code point order, and conversion to and from UTF-8 that always produces
	// well-formed text. The engine's own VspString stays the UTF-8 type of the
	// file system, of the log line and of the C ABI: the moment text is something
	// a READER sees, it is a UnicodeString, and the two meet only at the edges
	// (FromUtf8 / ToUtf8).
	//
	// A LOCALE is an icu::Locale, so "zh-CN", "zh_Hans_CN" and "zh-CN-u-nu-latn"
	// are understood and compared by ICU itself rather than by string surgery on
	// the engine's side. The system locale comes from ICU too
	// (icu::Locale::getDefault()), so the engine asks one library what the
	// machine is set to.
	//
	// A CATALOG maps a stable KEY ("ui.button.start") to the text a reader of
	// that locale should see. Lookup falls back to the FALLBACK locale and, when
	// even that has no entry, returns the key itself - a missing translation must
	// never blank a UI, it must show something a developer can act on.
	//
	// Catalogs are JSON files, one per locale, in a locale directory:
	//
	//     <directory>/zh-CN.json
	//     {
	//         "locale": "zh-CN",
	//         "entries":
	//         {
	//             "ui.button.start": "开始",
	//             "ui.hud.fps": "帧率 {0}"
	//         }
	//     }
	//
	// TranslateFormat() looks the key up and then runs the text through
	// VspFormat, so a catalog can carry "{0}" placeholders that the caller fills
	// in - which is what keeps word order a property of the translation. The
	// engine's own formatter does that job rather than ICU's MessageFormat
	// because MessageFormat reads the ICU locale DATA files, and this build links
	// ICU from a static library with the data stubbed out (UCONFIG_NO_FILE_IO +
	// stubdata.cpp, see Engine/Source/Thirdparty/ICU/icu4c-77_1/
	// BuildToStaticLibrary/CMakeLists.txt): UnicodeString and Locale - the parts
	// of ICU this service uses - need no data at all.
	//
	// The service is a singleton (Get()) because the locale is process-wide
	// state, exactly like the log. Every function reports failure through its
	// return value and logs the reason; nothing throws.
	// -------------------------------------------------------------------------
#pragma warning(push)
#pragma warning(disable : 4251)   // ICU and ArrayList members of the exported facade.
	class RUNTIME_API I18N
	{
	public:
		static I18N& Get();

		// -------- Locale --------
		// The locale lookups read first. An empty tag means the root locale, and
		// the overloads accepting UTF-8 convert at the edge.
		void SetLocale(const icu::Locale& Locale);
		void SetLocale(const icu::UnicodeString& LocaleCode);
		void SetLocale(const VspString& sLocaleCodeUtf8);

		// The locale itself - the object ICU resolves language, script and
		// country from.
		const icu::Locale& GetLocale() const { return m_Locale; }

		// Its canonical name, as ICU spells it ("zh_CN"), in both string types.
		icu::UnicodeString GetLocaleCode() const;
		VspString GetLocaleCodeUtf8() const;

		// The locale consulted when the current one has no entry for a key.
		void SetFallbackLocale(const icu::Locale& Locale);
		void SetFallbackLocale(const icu::UnicodeString& LocaleCode);
		void SetFallbackLocale(const VspString& sLocaleCodeUtf8);
		const icu::Locale& GetFallbackLocale() const { return m_FallbackLocale; }
		icu::UnicodeString GetFallbackLocaleCode() const;

		// The locale the operating system is configured for, asked of ICU
		// (icu::Locale::getDefault()).
		static icu::Locale DetectSystemLocale();
		static VspString DetectSystemLocaleUtf8();

		// True for locales written right to left (Arabic, Hebrew, Persian,
		// Urdu, ...), which a layout has to mirror. The decision is made from the
		// locale's own language and script subtags, which are part of the tag and
		// therefore always available.
		bool IsRightToLeft() const;
		static bool IsRightToLeftLocale(const icu::Locale& Locale);

		// -------- Catalog --------
		// Adds or replaces one entry of one locale. The UTF-8 overload converts
		// key and text through ICU.
		bool AddEntry(const icu::Locale& Locale, const icu::UnicodeString& Key, const icu::UnicodeString& Text);
		bool AddEntry(const VspString& sLocaleCodeUtf8, const VspString& sKeyUtf8, const VspString& sTextUtf8);

		// Forgets every entry (the engine's shutdown path).
		void ClearEntries();

		uint32 GetEntryCount() const { return static_cast<uint32>(m_Entries.GetSize()); }

		// Number of entries that belong to one locale (diagnostics).
		uint32 GetEntryCountForLocale(const icu::Locale& Locale) const;

		// -------- Lookup --------
		// True when the key resolves in the current locale or in the fallback.
		bool TryTranslate(const icu::UnicodeString& Key, icu::UnicodeString& outText) const;
		bool TryTranslate(const VspString& sKeyUtf8, icu::UnicodeString& outText) const;

		// The translation, the fallback's translation, or the key itself. This
		// never returns an empty string for a non-empty key.
		icu::UnicodeString Translate(const icu::UnicodeString& Key) const;
		icu::UnicodeString Translate(const VspString& sKeyUtf8) const;

		// The same result in the engine's UTF-8 string, for a caller that has to
		// hand it to a file, a log line or the C ABI.
		VspString TranslateUtf8(const VspString& sKeyUtf8) const;

		// Lookup followed by VspFormat, for catalogs that carry "{0}" style
		// placeholders.
		template <typename... ArgumentTypes>
		icu::UnicodeString TranslateFormat(const icu::UnicodeString& Key, const ArgumentTypes&... arguments) const
		{
			const VspString sPatternUtf8 = ToUtf8(Translate(Key));
			return FromUtf8(VspFormat::Format(sPatternUtf8.GetData(), arguments...));
		}

		template <typename... ArgumentTypes>
		icu::UnicodeString TranslateFormat(const VspString& sKeyUtf8, const ArgumentTypes&... arguments) const
		{
			return TranslateFormat(FromUtf8(sKeyUtf8), arguments...);
		}

		// -------- Loading --------
		// Reads one catalog file. The locale it belongs to is taken from its
		// "locale" member, or from the file's own name when that member is
		// missing.
		bool LoadCatalogFile(const VspString& sFilePathUtf8);

		// Reads the catalog of the current and of the fallback locale out of a
		// directory. ICU spells a locale canonically ("zh_CN") while a file is
		// usually named the way its tag is written ("zh-CN"), so BOTH spellings
		// are looked for. A locale without a file is not an error - the other
		// one, and finally the keys themselves, carry the UI.
		bool LoadCatalogDirectory(const VspString& sDirectoryPathUtf8);

		// The catalog file of one locale inside a directory, in either spelling;
		// empty when neither exists.
		static VspString FindCatalogFilePath(const VspString& sDirectoryPathUtf8, const icu::Locale& Locale);

		// Where LoadCatalogDirectory looks by default:
		// "<executable directory>/Locales".
		static VspString GetDefaultCatalogDirectoryPath();

		// -------- Conversion (the engine's Unicode authority) --------
		// UTF-8 in, UnicodeString out. Malformed bytes become U+FFFD rather than
		// failing, so a conversion always produces well-formed text.
		static icu::UnicodeString FromUtf8(const VspString& sTextUtf8);
		static icu::UnicodeString FromUtf8(const char* pTextUtf8);

		// UnicodeString in, UTF-8 out (the engine's file/log/ABI encoding).
		static VspString ToUtf8(const icu::UnicodeString& Text);

	private:
		I18N() = default;

		// One translated string: the locale it belongs to, its key and the text.
		// All three are ICU objects, so a comparison is ICU's and not the
		// engine's.
		struct Entry
		{
			icu::Locale Locale;
			icu::UnicodeString Key;
			icu::UnicodeString Text;
		};

		const Entry* FindEntry(const icu::Locale& Locale, const icu::UnicodeString& Key) const;

		// Reports a key that has no translation of its own - once per key, so a
		// widget that asks for its label every frame cannot bury the log.
		void ReportMissingKey(const icu::UnicodeString& Key) const;

		// How many distinct missing keys are named before the service stops
		// reporting them (a catalog that is missing altogether would otherwise
		// grow the list for every key a UI ever asks for).
		static constexpr size_t k_nMaximumMissingKeyReportCount = 64;

		icu::Locale m_Locale;
		icu::Locale m_FallbackLocale;
		ArrayList<Entry> m_Entries;
		mutable ArrayList<icu::UnicodeString> m_ReportedMissingKeys;
	};
#pragma warning(pop)
}
