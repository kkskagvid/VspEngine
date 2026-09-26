using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;

namespace VspEngine
{
	/// <summary>
	/// Raw P/Invoke bindings of the native localisation service (VspI18N_* in
	/// VspCore). <see cref="I18N"/> is the managed facade meant to be used.
	///
	/// The native service holds its text as ICU <c>UnicodeString</c> objects, so
	/// a translation can come back either way: as UTF-8 (the engine's file, log
	/// and ABI encoding, and what the locale codes use) or as UTF-16 (the
	/// encoding a UnicodeString already holds - and the one a .NET
	/// <see cref="string"/> is made of, so nothing is converted at all).
	/// </summary>
	internal static class I18NApi
	{
		private const string LibraryName = "VspCore";

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspI18N_SetLocale([MarshalAs(UnmanagedType.LPUTF8Str)] string? localeCodeUtf8);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspI18N_GetLocale(byte[] bufferUtf8, int bufferCapacityBytes);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspI18N_SetFallbackLocale([MarshalAs(UnmanagedType.LPUTF8Str)] string? localeCodeUtf8);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspI18N_GetFallbackLocale(byte[] bufferUtf8, int bufferCapacityBytes);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspI18N_DetectSystemLocale(byte[] bufferUtf8, int bufferCapacityBytes);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspI18N_IsRightToLeft();

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspI18N_IsRightToLeftLocale([MarshalAs(UnmanagedType.LPUTF8Str)] string localeCodeUtf8);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspI18N_AddEntry(
			[MarshalAs(UnmanagedType.LPUTF8Str)] string localeCodeUtf8,
			[MarshalAs(UnmanagedType.LPUTF8Str)] string keyUtf8,
			[MarshalAs(UnmanagedType.LPUTF8Str)] string textUtf8);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspI18N_GetEntryCount();

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspI18N_ClearEntries();

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspI18N_TryTranslate(
			[MarshalAs(UnmanagedType.LPUTF8Str)] string keyUtf8, byte[] bufferUtf8, int bufferCapacityBytes);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspI18N_Translate(
			[MarshalAs(UnmanagedType.LPUTF8Str)] string keyUtf8, byte[] bufferUtf8, int bufferCapacityBytes);

		/// <summary>
		/// The same two lookups, writing UTF-16 code units straight out of the
		/// native ICU UnicodeString into a caller-owned <c>char[]</c>.
		///
		/// <c>CharSet.Unicode</c> is what makes that a plain copy: without it the
		/// interop marshaller would treat the <c>char[]</c> as an ANSI byte buffer
		/// and squeeze every UTF-16 unit into one byte. The <c>string</c>
		/// parameters are unaffected - they name their own marshalling.
		/// </summary>
		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
		internal static extern int VspI18N_TryTranslateUtf16(
			[MarshalAs(UnmanagedType.LPUTF8Str)] string keyUtf8, [Out] char[] bufferUtf16, int bufferCapacityUnits);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
		internal static extern int VspI18N_TranslateUtf16(
			[MarshalAs(UnmanagedType.LPUTF8Str)] string keyUtf8, [Out] char[] bufferUtf16, int bufferCapacityUnits);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspI18N_LoadCatalogFile([MarshalAs(UnmanagedType.LPUTF8Str)] string filePathUtf8);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspI18N_LoadCatalogDirectory([MarshalAs(UnmanagedType.LPUTF8Str)] string directoryPathUtf8);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspI18N_GetDefaultCatalogDirectory(byte[] bufferUtf8, int bufferCapacityBytes);
	}

	/// <summary>
	/// The game-facing localisation facade. Every string a player reads goes
	/// through it:
	///
	///     I18N.Locale = "zh-CN";
	///     I18N.LoadDefaultCatalog();
	///     string label = I18N.Translate("ui.button.start");
	///     string fps   = I18N.Translate("ui.hud.fps", frameCount);
	///
	/// The service lives natively (<c>Core/String/I18N</c> in VspCore) and holds
	/// its text as ICU <c>UnicodeString</c> objects, and its locales as ICU
	/// <c>Locale</c> objects - so "zh-CN" and "zh_Hans_CN" are the same locale to
	/// ICU, and a translated string is Unicode-correct however it was written.
	/// That is also why a translation crosses the boundary as UTF-16 code units:
	/// it is the encoding the native string already holds and the one a .NET
	/// string is made of, so the text arrives with no conversion in between.
	///
	/// Resolution order is fixed: the current locale first, the fallback locale
	/// second, and the key itself when neither has an entry - a missing
	/// translation shows something a developer can act on instead of blanking a
	/// UI.
	///
	/// A catalog is a JSON file per locale (<c>Locales/zh-CN.json</c>) holding a
	/// flat "entries" object of key to text; placeholder syntax is the engine's
	/// <c>{0}</c> form, which <see cref="Translate(string, object?[])"/> fills in
	/// with <c>string.Format</c>.
	/// </summary>
	public static class I18N
	{
		// Text the native getters may write into. One kilobyte of UTF-16 units
		// holds a long UI label; anything longer is truncated rather than
		// rejected.
		private const int TextBufferUnitCount = 1024;

		// Locale codes are ASCII, so they travel as UTF-8 and need far less room.
		private const int LocaleBufferByteCount = 256;

		private static readonly char[] textBuffer = new char[TextBufferUnitCount];
		private static readonly byte[] localeBuffer = new byte[LocaleBufferByteCount];

		// Keys whose translation a format already failed on, so the report is
		// written once instead of once per frame.
		private static readonly HashSet<string> reportedFormatFailures = new HashSet<string>();

		/// <summary>
		/// The locale lookups read first. Setting it takes effect immediately for
		/// every later <see cref="Translate(string)"/>.
		/// </summary>
		public static string Locale
		{
			get => ReadUtf8Buffer(I18NApi.VspI18N_GetLocale(localeBuffer, localeBuffer.Length));
			set => I18NApi.VspI18N_SetLocale(value);
		}

		/// <summary>The locale consulted when the current one has no entry.</summary>
		public static string FallbackLocale
		{
			get => ReadUtf8Buffer(I18NApi.VspI18N_GetFallbackLocale(localeBuffer, localeBuffer.Length));
			set => I18NApi.VspI18N_SetFallbackLocale(value);
		}

		/// <summary>The locale the operating system is configured for.</summary>
		public static string SystemLocale => ReadUtf8Buffer(I18NApi.VspI18N_DetectSystemLocale(localeBuffer, localeBuffer.Length));

		/// <summary>
		/// True for locales written right to left, which a layout has to mirror.
		/// </summary>
		public static bool IsRightToLeft => I18NApi.VspI18N_IsRightToLeft() != 0;

		/// <summary>Number of entries loaded across every locale.</summary>
		public static int EntryCount => I18NApi.VspI18N_GetEntryCount();

		/// <summary>The directory <see cref="LoadDefaultCatalog"/> reads from.</summary>
		public static string DefaultCatalogDirectory =>
			ReadUtf8Buffer(I18NApi.VspI18N_GetDefaultCatalogDirectory(localeBuffer, localeBuffer.Length));

		/// <summary>Adds or replaces one entry of one locale.</summary>
		public static bool AddEntry(string localeCode, string key, string text) =>
			I18NApi.VspI18N_AddEntry(localeCode, key, text) != 0;

		/// <summary>Forgets every loaded entry.</summary>
		public static void ClearEntries() => I18NApi.VspI18N_ClearEntries();

		/// <summary>True when the key resolves in the current or the fallback locale.</summary>
		public static bool TryTranslate(string key, out string text)
		{
			int unitCount = I18NApi.VspI18N_TryTranslateUtf16(key, textBuffer, textBuffer.Length);
			text = unitCount > 0 ? new string(textBuffer, 0, unitCount) : string.Empty;
			return unitCount > 0;
		}

		/// <summary>
		/// The translation, the fallback's translation, or the key itself - this
		/// never returns an empty string for a non-empty key.
		/// </summary>
		public static string Translate(string key)
		{
			if (string.IsNullOrEmpty(key))
			{
				return string.Empty;
			}

			// The text arrives as the UTF-16 code units the native UnicodeString
			// already holds: no decoding, and a character outside the basic plane
			// keeps its surrogate pair, which is exactly what a .NET string wants.
			int unitCount = I18NApi.VspI18N_TranslateUtf16(key, textBuffer, textBuffer.Length);
			return unitCount > 0 ? new string(textBuffer, 0, unitCount) : string.Empty;
		}

		/// <summary>
		/// The translation with its <c>{0}</c> placeholders filled in, which is
		/// what keeps word order a property of the translation rather than of the
		/// code.
		/// </summary>
		public static string Translate(string key, params object?[] arguments)
		{
			string text = Translate(key);
			if (arguments == null || arguments.Length == 0)
			{
				return text;
			}

			try
			{
				return string.Format(text, arguments);
			}
			catch (FormatException)
			{
				// A catalog with a malformed placeholder must not take the UI
				// down: the unformatted text is still readable. The mistake is
				// reported ONCE per key - a UI formats its labels every frame, and
				// repeating the message would bury the log it has to appear in.
				if (reportedFormatFailures.Add(key))
				{
					Debug.LogError("I18N: the entry '" + key + "' has a malformed placeholder; it is shown unformatted.");
				}
				return text;
			}
		}

		/// <summary>Reads one catalog file.</summary>
		public static bool LoadCatalogFile(string filePath) => I18NApi.VspI18N_LoadCatalogFile(filePath) != 0;

		/// <summary>Reads the catalogs of the current and the fallback locale.</summary>
		public static bool LoadCatalogDirectory(string directoryPath) =>
			I18NApi.VspI18N_LoadCatalogDirectory(directoryPath) != 0;

		/// <summary>
		/// Reads the catalogs next to the executable (its "Locales" folder).
		/// A locale without a file is not a failure: the other locale and the
		/// keys themselves still carry the UI.
		/// </summary>
		public static bool LoadDefaultCatalog()
		{
			string directory = DefaultCatalogDirectory;
			return !string.IsNullOrEmpty(directory) && LoadCatalogDirectory(directory);
		}

		/// <summary>Turns the native byte count into a string (locale codes).</summary>
		private static string ReadUtf8Buffer(int byteCount)
		{
			if (byteCount <= 0)
			{
				return string.Empty;
			}
			return Encoding.UTF8.GetString(localeBuffer, 0, byteCount);
		}
	}
}
