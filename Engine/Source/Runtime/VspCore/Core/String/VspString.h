#pragma once

#include <cstring>
#include <functional>
#include <string>
#include <type_traits>
#include <utility>

#include "Core/Core.h"
#include "Core/Templates/ArrayList.h"

namespace Vsp
{
	// Forward declaration: VspStringWrapper stores code units only, while its
	// transcoding members need a complete VspString, so those are defined
	// after the VspString class at the bottom of this header.
	class VspString;

	// =========================================================================
	// VspStringWrapper
	// =========================================================================

	// Internal trait: the code unit types a VspStringWrapper can store.
	namespace StringDetail
	{
		template <typename CodeUnitType>
		inline constexpr bool bIsSupportedCodeUnitType =
			std::is_same_v<CodeUnitType, char> ||
			std::is_same_v<CodeUnitType, char8_t> ||
			std::is_same_v<CodeUnitType, char16_t> ||
			std::is_same_v<CodeUnitType, char32_t> ||
			std::is_same_v<CodeUnitType, wchar_t>;
	}

	// -------------------------------------------------------------------------
	// VspStringWrapper<CodeUnitType>
	// -------------------------------------------------------------------------
	// Code-unit-typed Unicode string. Instantiating it over different code unit
	// types is how the engine produces its concrete Unicode string types:
	//     VspStringWrapper<char>      -> UTF-8  code units
	//     VspStringWrapper<char8_t>   -> UTF-8  code units (u8 literals)
	//     VspStringWrapper<char16_t>  -> UTF-16 code units
	//     VspStringWrapper<char32_t>  -> UTF-32 code points
	//     VspStringWrapper<wchar_t>   -> UTF-16 on Windows, UTF-32 elsewhere
	//
	// The stored units live in an ArrayList and are always followed by a null
	// terminator, so GetData() can be handed to any C-style API directly.
	// Conversions to and from the UTF-8 based VspString transcode the content,
	// which is why they are named operations (ToVspString/FromVspString) rather
	// than implicit conversions. Like the rest of the runtime, this class never
	// throws: misuse is reported with DEBUG_BREAK().
	//
	// Members that need a complete VspString (the transcoding ones) are only
	// declared here and defined after the VspString class further down.
	// -------------------------------------------------------------------------
	template <typename CodeUnitType>
	class VspStringWrapper
	{
	public:
		using SizeType = size_t;
		using CodePointType = char32_t;

		static_assert(StringDetail::bIsSupportedCodeUnitType<CodeUnitType>,
			"VspStringWrapper requires char, char8_t, char16_t, char32_t or wchar_t as its code unit type.");

		// Sentinel returned by Find() when nothing matched.
		static constexpr SizeType InvalidIndex = static_cast<SizeType>(-1);

		// -------- Construction --------
		VspStringWrapper()
		{
			m_Data.Add(CodeUnitType(0));
		}

		VspStringWrapper(std::nullptr_t)
			: VspStringWrapper()
		{
		}

		// Transcodes the UTF-8 based text into this wrapper's code units.
		explicit VspStringWrapper(const VspString& Utf8Text);

		// Copies the null-terminated text.
		VspStringWrapper(const CodeUnitType* pText)
			: VspStringWrapper()
		{
			if (pText != nullptr)
			{
				SizeType nUnitCount = 0;
				while (pText[nUnitCount] != CodeUnitType(0))
				{
					++nUnitCount;
				}
				AppendUnits(pText, nUnitCount);
			}
		}

		// Copies exactly nUnitCount units, even when they contain embedded nulls.
		VspStringWrapper(const CodeUnitType* pUnits, SizeType nUnitCount)
			: VspStringWrapper()
		{
			AppendUnits(pUnits, nUnitCount);
		}

		// Constructs a string holding a single code unit (not a code point).
		explicit VspStringWrapper(CodeUnitType SingleUnit)
			: VspStringWrapper()
		{
			AppendUnits(&SingleUnit, 1);
		}

		VspStringWrapper(const VspStringWrapper& Other)
			: m_Data(Other.m_Data)
		{
		}

		VspStringWrapper(VspStringWrapper&& Other)
			: m_Data(std::move(Other.m_Data))
		{
			// Restore the moved-from object's terminator invariant.
			Other.m_Data.Add(CodeUnitType(0));
		}

		~VspStringWrapper() {}

		// -------- Assignment --------
		VspStringWrapper& operator=(const VspStringWrapper& Other)
		{
			if (this != &Other)
			{
				m_Data = Other.m_Data;
			}
			return *this;
		}

		VspStringWrapper& operator=(VspStringWrapper&& Other)
		{
			if (this != &Other)
			{
				m_Data = std::move(Other.m_Data);
				Other.m_Data.Add(CodeUnitType(0));
			}
			return *this;
		}

		// Empties the string and releases the stored units.
		VspStringWrapper& operator=(std::nullptr_t)
		{
			return Clear();
		}

		// Replaces the content with the transcoded UTF-8 text.
		VspStringWrapper& operator=(const VspString& Utf8Text);

		VspStringWrapper& operator=(const CodeUnitType* pText)
		{
			Clear();
			if (pText != nullptr)
			{
				SizeType nUnitCount = 0;
				while (pText[nUnitCount] != CodeUnitType(0))
				{
					++nUnitCount;
				}
				AppendUnits(pText, nUnitCount);
			}
			return *this;
		}

		// -------- Conversion to/from VspString --------
		static VspStringWrapper FromVspString(const VspString& Utf8Text);

		// Transcodes the stored units into a UTF-8 based VspString.
		VspString ToVspString() const;

		// -------- Observers --------
		// Number of code units, excluding the null terminator.
		SizeType GetLength() const
		{
			return m_Data.GetSize() - 1;
		}

		bool IsEmpty() const
		{
			return m_Data.GetSize() <= 1;
		}

		// Null-terminated code unit buffer.
		const CodeUnitType* GetData() const
		{
			return m_Data.GetData();
		}

		// Number of Unicode code points. UTF-32 units are counted directly;
		// other encodings are decoded through VspString.
		SizeType GetCodePointCount() const;

		// -------- Modification --------
		VspStringWrapper& Append(const VspStringWrapper& Other)
		{
			AppendUnits(Other.m_Data.GetData(), Other.GetLength());
			return *this;
		}

		// Appends the UTF-8 text after transcoding it into this wrapper's units.
		VspStringWrapper& Append(const VspString& Utf8Text);

		VspStringWrapper& Append(const CodeUnitType* pText)
		{
			if (pText != nullptr)
			{
				SizeType nUnitCount = 0;
				while (pText[nUnitCount] != CodeUnitType(0))
				{
					++nUnitCount;
				}
				AppendUnits(pText, nUnitCount);
			}
			return *this;
		}

		VspStringWrapper& Append(const CodeUnitType* pUnits, SizeType nUnitCount)
		{
			AppendUnits(pUnits, nUnitCount);
			return *this;
		}

		// Appends a single code unit (not a code point).
		VspStringWrapper& Append(CodeUnitType SingleUnit)
		{
			return Append(&SingleUnit, 1);
		}

		// Appends a single code point, encoded into this wrapper's units.
		// Invalid code points (surrogates, > U+10FFFF) become U+FFFD.
		VspStringWrapper& AppendCodePoint(CodePointType uCodePoint);

		VspStringWrapper& Clear()
		{
			m_Data.Clear();
			m_Data.Add(CodeUnitType(0));
			return *this;
		}

		// Ensures capacity for at least nUnitCapacity code units (plus terminator).
		VspStringWrapper& Reserve(SizeType nUnitCapacity)
		{
			m_Data.Reserve(nUnitCapacity + 1);
			return *this;
		}

		VspStringWrapper& operator+=(const VspStringWrapper& Other)
		{
			return Append(Other);
		}

		VspStringWrapper& operator+=(const VspString& Utf8Text)
		{
			return Append(Utf8Text);
		}

		VspStringWrapper& operator+=(const CodeUnitType* pText)
		{
			return Append(pText);
		}

		VspStringWrapper& operator+=(CodeUnitType SingleUnit)
		{
			return Append(&SingleUnit, 1);
		}

		// -------- Comparison --------
		bool Equals(const VspStringWrapper& Other) const
		{
			SizeType nUnitCount = GetLength();
			if (nUnitCount != Other.GetLength())
			{
				return false;
			}
			return nUnitCount == 0 || memcmp(m_Data.GetData(), Other.m_Data.GetData(), nUnitCount * sizeof(CodeUnitType)) == 0;
		}

		bool Equals(const CodeUnitType* pText) const
		{
			if (pText == nullptr)
			{
				return false;
			}

			SizeType nOtherUnitCount = 0;
			while (pText[nOtherUnitCount] != CodeUnitType(0))
			{
				++nOtherUnitCount;
			}

			if (GetLength() != nOtherUnitCount)
			{
				return false;
			}
			return nOtherUnitCount == 0 || memcmp(m_Data.GetData(), pText, nOtherUnitCount * sizeof(CodeUnitType)) == 0;
		}

		// Compares code point sequences; transcodes this wrapper to UTF-8 first.
		bool Equals(const VspString& Utf8Text) const;

		// -------- Search --------
		// Returns the index of the first matching code unit, or InvalidIndex.
		SizeType Find(CodeUnitType Unit, SizeType nStartUnitIndex = 0) const
		{
			SizeType nUnitCount = GetLength();
			if (nStartUnitIndex > nUnitCount)
			{
				DEBUG_BREAK();
				nStartUnitIndex = nUnitCount;
			}

			for (SizeType nUnitIndex = nStartUnitIndex; nUnitIndex < nUnitCount; ++nUnitIndex)
			{
				if (m_Data[nUnitIndex] == Unit)
				{
					return nUnitIndex;
				}
			}
			return InvalidIndex;
		}

		bool Contains(CodeUnitType Unit) const
		{
			return Find(Unit) != InvalidIndex;
		}

		bool StartsWith(const CodeUnitType* pPrefixText) const
		{
			if (pPrefixText == nullptr)
			{
				return false;
			}

			SizeType nPrefixUnitCount = 0;
			while (pPrefixText[nPrefixUnitCount] != CodeUnitType(0))
			{
				++nPrefixUnitCount;
			}

			if (nPrefixUnitCount > GetLength())
			{
				return false;
			}
			return nPrefixUnitCount == 0 || memcmp(m_Data.GetData(), pPrefixText, nPrefixUnitCount * sizeof(CodeUnitType)) == 0;
		}

		bool EndsWith(const CodeUnitType* pSuffixText) const
		{
			if (pSuffixText == nullptr)
			{
				return false;
			}

			SizeType nSuffixUnitCount = 0;
			while (pSuffixText[nSuffixUnitCount] != CodeUnitType(0))
			{
				++nSuffixUnitCount;
			}

			SizeType nUnitCount = GetLength();
			if (nSuffixUnitCount > nUnitCount)
			{
				return false;
			}
			return nSuffixUnitCount == 0 ||
				memcmp(m_Data.GetData() + nUnitCount - nSuffixUnitCount, pSuffixText, nSuffixUnitCount * sizeof(CodeUnitType)) == 0;
		}

		// -------- Iteration --------
		// Invokes Callback for every code point, in string order. The content is
		// decoded through VspString, so malformed units appear as U+FFFD.
		void ForEachCodePoint(const std::function<void(CodePointType)>& Callback) const;

	private:
		// Appends raw units in front of the terminator and re-adds it.
		void AppendUnits(const CodeUnitType* pUnits, SizeType nUnitCount)
		{
			if (pUnits == nullptr || nUnitCount == 0)
			{
				return;
			}

			m_Data.Reserve(m_Data.GetSize() + nUnitCount);
			m_Data.PopBack();   // Drop the terminator.
			for (SizeType nUnitIndex = 0; nUnitIndex < nUnitCount; ++nUnitIndex)
			{
				m_Data.Add(pUnits[nUnitIndex]);
			}
			m_Data.Add(CodeUnitType(0));
		}

		// Encodes a single code point into this wrapper's code units.
		void AppendEncodedCodePoint(CodePointType uCodePoint);

	private:
		// The stored code units; the last element is always the null terminator.
		ArrayList<CodeUnitType> m_Data;
	};

	// -------------------------------------------------------------------------
	// Ready-made Unicode string types built on VspStringWrapper.
	// -------------------------------------------------------------------------
	using Utf8String = VspStringWrapper<char>;
	using Utf16String = VspStringWrapper<char16_t>;
	using Utf32String = VspStringWrapper<char32_t>;
	using WideString = VspStringWrapper<wchar_t>;

	// =========================================================================
	// VspString
	// =========================================================================

	// -------------------------------------------------------------------------
	// VspString
	// -------------------------------------------------------------------------
	// Unicode string whose content is always stored as UTF-8 bytes. The byte
	// buffer itself is owned and managed by VspStringWrapper<char> (alias
	// Utf8String), which keeps it permanently null-terminated, so this class
	// focuses on Unicode semantics: code point access, UTF-16/UTF-32
	// transcoding and text queries.
	//
	// Text is addressed in two ways:
	//   - Byte offsets / byte counts refer to raw UTF-8 bytes.
	//   - Code point indices / code point counts refer to Unicode code points.
	//
	// Conversions to and from UTF-16 and UTF-32 are provided both as object
	// conversions (FromUtf16/FromUtf32/ToUtf16/ToUtf32) and as raw buffer
	// transcoding helpers (ConvertXxxToYyy). Malformed input is replaced with
	// U+FFFD instead of failing, so every conversion always makes progress.
	//
	// This class never uses C++ exceptions: misuse is reported with
	// DEBUG_BREAK() and results are clamped, mirroring ArrayList.
	// -------------------------------------------------------------------------
#pragma warning(push)
#pragma warning(disable : 4251)   // VspStringWrapper<char> member: its members are inline and header-only.
	class RUNTIME_API VspString
	{
	public:
		using SizeType = size_t;
		using CodePointType = char32_t;
		using Utf16UnitType = char16_t;
		using Utf32UnitType = char32_t;
		using WideUnitType = wchar_t;

		// Sentinel returned by the search functions when nothing matched.
		static constexpr SizeType InvalidIndex = static_cast<SizeType>(-1);

		// -------- Construction --------
		VspString();
		VspString(std::nullptr_t);

		// Copies the null-terminated UTF-8 text.
		VspString(const char* pUtf8Text);
		VspString(const char8_t* pUtf8Text);

		// Copies exactly nByteCount bytes, even when they contain embedded nulls.
		// The bytes are not validated; use IsValidUtf8() to check them.
		VspString(const char* pUtf8Bytes, SizeType nByteCount);

		// Copies the null-terminated UTF-16 / UTF-32 / wide text, transcoding
		// it into the UTF-8 storage (wide = UTF-16 on Windows, UTF-32 elsewhere).
		VspString(const char16_t* pUtf16Text);
		VspString(const char32_t* pUtf32Text);
		VspString(const wchar_t* pWideText);

		VspString(const std::string& Utf8String);

		// Constructs a string holding a single code point.
		explicit VspString(CodePointType uCodePoint);

		VspString(const VspString& Other);
		VspString(VspString&& Other);
		~VspString();

		// -------- Static factories for UTF-16 / UTF-32 input --------
		static VspString FromUtf16(const char16_t* pUtf16Text);                      // Null-terminated input.
		static VspString FromUtf16(const char16_t* pUtf16Units, SizeType nUnitCount);
		static VspString FromUtf32(const char32_t* pUtf32Text);                      // Null-terminated input.
		static VspString FromUtf32(const char32_t* pUtf32Units, SizeType nUnitCount);
		static VspString FromWideText(const wchar_t* pWideText);                     // Null-terminated input.
		static VspString FromWideText(const wchar_t* pWideUnits, SizeType nWideUnitCount);

		// -------- Assignment --------
		VspString& operator=(const VspString& Other);
		VspString& operator=(VspString&& Other);

		// Empties the string, releasing the stored UTF-8 bytes.
		VspString& operator=(std::nullptr_t);

		VspString& operator=(const char* pUtf8Text);
		VspString& operator=(const std::string& Utf8String);

		// -------- Conversion to other encodings --------
		ArrayList<char16_t> ToUtf16() const;    // Null-terminated UTF-16 code units.
		ArrayList<char32_t> ToUtf32() const;    // Null-terminated UTF-32 code points.
		ArrayList<wchar_t> ToWideText() const;  // UTF-16 on Windows, UTF-32 elsewhere; null-terminated.
		std::string ToStdString() const;        // Raw UTF-8 bytes in a std::string.

		// -------- Raw transcoding between encodings --------
		// Each helper transcodes nXxxCount source units and returns the total
		// number of output units required for the whole input. When pOutUnits is
		// not null, up to nOutCapacity units are written and no terminator is
		// appended. A null source pointer returns 0. Malformed sequences are
		// replaced with U+FFFD, so the result is always well-formed text.
		static SizeType ConvertUtf8ToUtf16(const char* pUtf8Bytes, SizeType nUtf8ByteCount, char16_t* pOutUtf16Units, SizeType nOutCapacity);
		static SizeType ConvertUtf8ToUtf32(const char* pUtf8Bytes, SizeType nUtf8ByteCount, char32_t* pOutUtf32Units, SizeType nOutCapacity);
		static SizeType ConvertUtf16ToUtf8(const char16_t* pUtf16Units, SizeType nUtf16UnitCount, char* pOutUtf8Bytes, SizeType nOutCapacity);
		static SizeType ConvertUtf32ToUtf8(const char32_t* pUtf32Units, SizeType nUtf32UnitCount, char* pOutUtf8Bytes, SizeType nOutCapacity);
		static SizeType ConvertUtf16ToUtf32(const char16_t* pUtf16Units, SizeType nUtf16UnitCount, char32_t* pOutUtf32Units, SizeType nOutCapacity);
		static SizeType ConvertUtf32ToUtf16(const char32_t* pUtf32Units, SizeType nUtf32UnitCount, char16_t* pOutUtf16Units, SizeType nOutCapacity);

		// -------- Validation --------
		bool IsValidUtf8() const;
		static bool IsValidUtf8(const char* pUtf8Bytes, SizeType nUtf8ByteCount);

		// -------- Observers --------
		SizeType GetByteLength() const;        // Number of UTF-8 bytes, excluding the terminator.
		SizeType GetCodePointCount() const;    // Number of Unicode code points.
		bool IsEmpty() const;
		const char* GetData() const;           // Null-terminated UTF-8 byte buffer.

		// -------- Code point access (indices count code points, not bytes) --------
		CodePointType GetCodePointAt(SizeType nCodePointIndex) const;   // Out-of-range: DEBUG_BREAK, then clamped to the last code point.
		CodePointType GetFirstCodePoint() const;
		CodePointType GetLastCodePoint() const;

		// Byte offset of the code point at nCodePointIndex. An index at or past
		// the end returns the byte length, which is the valid append position.
		SizeType GetCodePointByteOffset(SizeType nCodePointIndex) const;

		// -------- Modification --------
		VspString& Append(const VspString& Other);
		VspString& Append(const std::string& Utf8String);
		VspString& Append(const char* pUtf8Text);
		VspString& Append(const char8_t* pUtf8Text);
		VspString& Append(const char* pUtf8Bytes, SizeType nByteCount);
		VspString& AppendCodePoint(CodePointType uCodePoint);

		// Inserts before the given code point index; an index of GetCodePointCount()
		// appends at the end. Invalid code points (surrogates, > U+10FFFF) are
		// replaced with U+FFFD.
		VspString& InsertCodePoint(SizeType nCodePointIndex, CodePointType uCodePoint);

		VspString& RemoveCodePointAt(SizeType nCodePointIndex);
		VspString& RemoveCodePointRange(SizeType nFirstCodePointIndex, SizeType nCodePointCount);

		// Empties the string but keeps the allocated buffer for reuse.
		VspString& Clear();
		VspString& Reserve(SizeType nByteCapacity);
		VspString& ShrinkToFit();

		// Byte-based range: [nStartByteOffset, nStartByteOffset + nByteCount),
		// clamped to the string bounds.
		VspString GetSubString(SizeType nStartByteOffset, SizeType nByteCount) const;

		VspString& operator+=(const VspString& Other);
		VspString& operator+=(const std::string& Utf8String);
		VspString& operator+=(const char* pUtf8Text);
		VspString& operator+=(CodePointType uCodePoint);

		// -------- Comparison --------
		bool Equals(const VspString& Other) const;
		bool Equals(const char* pUtf8Text) const;
		bool Equals(const std::string& Utf8String) const;

		// Ordinal comparison by code point. Returns -1, 0 or 1.
		int CompareTo(const VspString& Other) const;

		// -------- Search --------
		SizeType Find(const std::string& NeedleUtf8, SizeType nStartByteOffset = 0) const;           // Byte offset, or InvalidIndex.
		SizeType FindCodePoint(CodePointType uCodePoint, SizeType nStartCodePointIndex = 0) const; // Code point index, or InvalidIndex.
		bool Contains(const std::string& NeedleUtf8) const;
		bool Contains(CodePointType uCodePoint) const;
		bool StartsWith(const std::string& PrefixUtf8) const;
		bool EndsWith(const std::string& SuffixUtf8) const;

		// -------- Iteration / utilities --------
		// Invokes Callback for every code point, in string order.
		void ForEachCodePoint(const std::function<void(CodePointType)>& Callback) const;

		// FNV-1a 64-bit hash over the raw UTF-8 bytes.
		uint64_t GetHashCode() const;

	private:
		// -------- Code point <-> code unit helpers --------
		// Decoders advance pCursor and report malformed input by returning false
		// (uOutCodePoint is then U+FFFD). Encoders sanitize invalid code points
		// to U+FFFD and return the number of units they wrote.
		static CodePointType SanitizeCodePoint(CodePointType uCodePoint);
		static bool DecodeUtf8CodePoint(const char*& pCursor, const char* pEnd, CodePointType& uOutCodePoint);
		static bool DecodeUtf16CodePoint(const char16_t*& pCursor, const char16_t* pEnd, CodePointType& uOutCodePoint);
		static SizeType EncodeCodePointToUtf8(CodePointType uCodePoint, char* pOutBytes);
		static SizeType EncodeCodePointToUtf16(CodePointType uCodePoint, char16_t* pOutUnits);
		static SizeType EncodeCodePointToUtf32(CodePointType uCodePoint, char32_t* pOutUnits);

	private:
		// The UTF-8 code units. Allocation, growth and null termination are all
		// managed by VspStringWrapper<char>.
		Utf8String m_Text;
	};
#pragma warning(pop)

	// -------- Comparison operators --------
	inline bool operator==(const VspString& Left, const VspString& Right) { return Left.Equals(Right); }
	inline bool operator!=(const VspString& Left, const VspString& Right) { return !Left.Equals(Right); }
	inline bool operator==(const VspString& Left, const char* pRightUtf8Text) { return Left.Equals(pRightUtf8Text); }
	inline bool operator==(const char* pLeftUtf8Text, const VspString& Right) { return Right.Equals(pLeftUtf8Text); }
	inline bool operator!=(const VspString& Left, const char* pRightUtf8Text) { return !Left.Equals(pRightUtf8Text); }
	inline bool operator!=(const char* pLeftUtf8Text, const VspString& Right) { return !Right.Equals(pLeftUtf8Text); }
	inline bool operator==(const VspString& Left, const std::string& RightUtf8) { return Left.Equals(RightUtf8); }
	inline bool operator==(const std::string& LeftUtf8, const VspString& Right) { return Right.Equals(LeftUtf8); }
	inline bool operator!=(const VspString& Left, const std::string& RightUtf8) { return !Left.Equals(RightUtf8); }
	inline bool operator!=(const std::string& LeftUtf8, const VspString& Right) { return !Right.Equals(LeftUtf8); }

	// -------- Concatenation operators --------
	inline VspString operator+(const VspString& Left, const VspString& Right)
	{
		VspString Result(Left);
		Result.Append(Right);
		return Result;
	}

	inline VspString operator+(const VspString& Left, const std::string& RightUtf8)
	{
		VspString Result(Left);
		Result.Append(RightUtf8);
		return Result;
	}

	inline VspString operator+(const std::string& LeftUtf8, const VspString& Right)
	{
		VspString Result(LeftUtf8);
		Result.Append(Right);
		return Result;
	}

	inline VspString operator+(const VspString& Left, const char* pRightUtf8Text)
	{
		VspString Result(Left);
		Result.Append(pRightUtf8Text);
		return Result;
	}

	inline VspString operator+(const char* pLeftUtf8Text, const VspString& Right)
	{
		VspString Result(pLeftUtf8Text);
		Result.Append(Right);
		return Result;
	}

	inline VspString operator+(const VspString& Left, char32_t uRightCodePoint)
	{
		VspString Result(Left);
		Result.AppendCodePoint(uRightCodePoint);
		return Result;
	}

	inline VspString operator+(char32_t uLeftCodePoint, const VspString& Right)
	{
		VspString Result(uLeftCodePoint);
		Result.Append(Right);
		return Result;
	}

	// =========================================================================
	// VspStringWrapper — deferred member definitions
	// =========================================================================
	// These members need a complete VspString (they transcode to or from it),
	// so they are defined here, after the VspString class.

	template <typename CodeUnitType>
	VspStringWrapper<CodeUnitType>::VspStringWrapper(const VspString& Utf8Text)
		: VspStringWrapper()
	{
		Append(Utf8Text);
	}

	template <typename CodeUnitType>
	VspStringWrapper<CodeUnitType>& VspStringWrapper<CodeUnitType>::operator=(const VspString& Utf8Text)
	{
		Clear();
		Append(Utf8Text);
		return *this;
	}

	template <typename CodeUnitType>
	VspStringWrapper<CodeUnitType> VspStringWrapper<CodeUnitType>::FromVspString(const VspString& Utf8Text)
	{
		return VspStringWrapper(Utf8Text);
	}

	template <typename CodeUnitType>
	VspString VspStringWrapper<CodeUnitType>::ToVspString() const
	{
		if constexpr (std::is_same_v<CodeUnitType, char> || std::is_same_v<CodeUnitType, char8_t>)
		{
			// UTF-8 units are already VspString's storage format.
			return VspString(reinterpret_cast<const char*>(m_Data.GetData()), GetLength());
		}
		else if constexpr (std::is_same_v<CodeUnitType, char16_t>)
		{
			return VspString::FromUtf16(m_Data.GetData(), GetLength());
		}
		else if constexpr (std::is_same_v<CodeUnitType, char32_t>)
		{
			return VspString::FromUtf32(m_Data.GetData(), GetLength());
		}
		else
		{
			return VspString::FromWideText(m_Data.GetData(), GetLength());
		}
	}

	template <typename CodeUnitType>
	VspStringWrapper<CodeUnitType>& VspStringWrapper<CodeUnitType>::Append(const VspString& Utf8Text)
	{
		if constexpr (std::is_same_v<CodeUnitType, char>)
		{
			AppendUnits(reinterpret_cast<const char*>(Utf8Text.GetData()), Utf8Text.GetByteLength());
		}
		else if constexpr (std::is_same_v<CodeUnitType, char8_t>)
		{
			AppendUnits(reinterpret_cast<const char8_t*>(Utf8Text.GetData()), Utf8Text.GetByteLength());
		}
		else if constexpr (std::is_same_v<CodeUnitType, char16_t>)
		{
			ArrayList<char16_t> Utf16Units = Utf8Text.ToUtf16();
			AppendUnits(Utf16Units.GetData(), Utf16Units.GetSize() - 1);
		}
		else if constexpr (std::is_same_v<CodeUnitType, char32_t>)
		{
			ArrayList<char32_t> Utf32Units = Utf8Text.ToUtf32();
			AppendUnits(Utf32Units.GetData(), Utf32Units.GetSize() - 1);
		}
		else
		{
			ArrayList<wchar_t> WideUnits = Utf8Text.ToWideText();
			AppendUnits(WideUnits.GetData(), WideUnits.GetSize() - 1);
		}
		return *this;
	}

	template <typename CodeUnitType>
	VspStringWrapper<CodeUnitType>& VspStringWrapper<CodeUnitType>::AppendCodePoint(CodePointType uCodePoint)
	{
		AppendEncodedCodePoint(uCodePoint);
		return *this;
	}

	template <typename CodeUnitType>
	bool VspStringWrapper<CodeUnitType>::Equals(const VspString& Utf8Text) const
	{
		return ToVspString().Equals(Utf8Text);
	}

	template <typename CodeUnitType>
	VspStringWrapper<CodeUnitType>::SizeType VspStringWrapper<CodeUnitType>::GetCodePointCount() const
	{
		if constexpr (std::is_same_v<CodeUnitType, char32_t>)
		{
			return GetLength();
		}
		else
		{
			return ToVspString().GetCodePointCount();
		}
	}

	template <typename CodeUnitType>
	void VspStringWrapper<CodeUnitType>::ForEachCodePoint(const std::function<void(CodePointType)>& Callback) const
	{
		ToVspString().ForEachCodePoint(Callback);
	}

	template <typename CodeUnitType>
	void VspStringWrapper<CodeUnitType>::AppendEncodedCodePoint(CodePointType uCodePoint)
	{
		// Surrogate halves and values above U+10FFFF are not valid code points.
		if (uCodePoint > 0x10FFFF || (uCodePoint >= 0xD800 && uCodePoint <= 0xDFFF))
		{
			uCodePoint = 0xFFFD;
		}

		if constexpr (std::is_same_v<CodeUnitType, char> || std::is_same_v<CodeUnitType, char8_t>)
		{
			// Delegate UTF-8 encoding to VspString, which owns the encoder.
			Append(VspString(uCodePoint));
		}
		else if constexpr (std::is_same_v<CodeUnitType, char32_t>)
		{
			CodeUnitType uSingleUnit = static_cast<CodeUnitType>(uCodePoint);
			AppendUnits(&uSingleUnit, 1);
		}
		else
		{
			// char16_t, or wchar_t when it is 16 bits wide (Windows).
			if constexpr (sizeof(CodeUnitType) == sizeof(char16_t))
			{
				CodeUnitType szEncodedUnits[2];
				SizeType nEncodedUnitCount = 1;
				if (uCodePoint <= 0xFFFF)
				{
					szEncodedUnits[0] = static_cast<CodeUnitType>(uCodePoint);
				}
				else
				{
					CodePointType uAdjustedCodePoint = uCodePoint - 0x10000;
					szEncodedUnits[0] = static_cast<CodeUnitType>(0xD800 + (uAdjustedCodePoint >> 10));
					szEncodedUnits[1] = static_cast<CodeUnitType>(0xDC00 + (uAdjustedCodePoint & 0x3FF));
					nEncodedUnitCount = 2;
				}
				AppendUnits(szEncodedUnits, nEncodedUnitCount);
			}
			else
			{
				// wchar_t is 32 bits wide (Linux/macOS).
				CodeUnitType uSingleUnit = static_cast<CodeUnitType>(uCodePoint);
				AppendUnits(&uSingleUnit, 1);
			}
		}
	}

	// -------- VspStringWrapper comparison operators --------
	template <typename CodeUnitType>
	inline bool operator==(const VspStringWrapper<CodeUnitType>& Left, const VspStringWrapper<CodeUnitType>& Right) { return Left.Equals(Right); }
	template <typename CodeUnitType>
	inline bool operator!=(const VspStringWrapper<CodeUnitType>& Left, const VspStringWrapper<CodeUnitType>& Right) { return !Left.Equals(Right); }
	template <typename CodeUnitType>
	inline bool operator==(const VspStringWrapper<CodeUnitType>& Left, const CodeUnitType* pRightText) { return Left.Equals(pRightText); }
	template <typename CodeUnitType>
	inline bool operator==(const CodeUnitType* pLeftText, const VspStringWrapper<CodeUnitType>& Right) { return Right.Equals(pLeftText); }
	template <typename CodeUnitType>
	inline bool operator!=(const VspStringWrapper<CodeUnitType>& Left, const CodeUnitType* pRightText) { return !Left.Equals(pRightText); }
	template <typename CodeUnitType>
	inline bool operator!=(const CodeUnitType* pLeftText, const VspStringWrapper<CodeUnitType>& Right) { return !Right.Equals(pLeftText); }
	template <typename CodeUnitType>
	inline bool operator==(const VspStringWrapper<CodeUnitType>& Left, const VspString& RightUtf8) { return Left.Equals(RightUtf8); }
	template <typename CodeUnitType>
	inline bool operator==(const VspString& LeftUtf8, const VspStringWrapper<CodeUnitType>& Right) { return Right.Equals(LeftUtf8); }
	template <typename CodeUnitType>
	inline bool operator!=(const VspStringWrapper<CodeUnitType>& Left, const VspString& RightUtf8) { return !Left.Equals(RightUtf8); }
	template <typename CodeUnitType>
	inline bool operator!=(const VspString& LeftUtf8, const VspStringWrapper<CodeUnitType>& Right) { return !Right.Equals(LeftUtf8); }

	// -------- VspStringWrapper concatenation operators --------
	template <typename CodeUnitType>
	inline VspStringWrapper<CodeUnitType> operator+(const VspStringWrapper<CodeUnitType>& Left, const VspStringWrapper<CodeUnitType>& Right)
	{
		VspStringWrapper<CodeUnitType> Result(Left);
		Result.Append(Right);
		return Result;
	}

	template <typename CodeUnitType>
	inline VspStringWrapper<CodeUnitType> operator+(const VspStringWrapper<CodeUnitType>& Left, const CodeUnitType* pRightText)
	{
		VspStringWrapper<CodeUnitType> Result(Left);
		Result.Append(pRightText);
		return Result;
	}

	template <typename CodeUnitType>
	inline VspStringWrapper<CodeUnitType> operator+(const CodeUnitType* pLeftText, const VspStringWrapper<CodeUnitType>& Right)
	{
		VspStringWrapper<CodeUnitType> Result(pLeftText);
		Result.Append(Right);
		return Result;
	}

	template <typename CodeUnitType>
	inline VspStringWrapper<CodeUnitType> operator+(const VspStringWrapper<CodeUnitType>& Left, const VspString& RightUtf8)
	{
		VspStringWrapper<CodeUnitType> Result(Left);
		Result.Append(RightUtf8);
		return Result;
	}

	template <typename CodeUnitType>
	inline VspStringWrapper<CodeUnitType> operator+(const VspString& LeftUtf8, const VspStringWrapper<CodeUnitType>& Right)
	{
		VspStringWrapper<CodeUnitType> Result(LeftUtf8);
		Result.Append(Right);
		return Result;
	}

	template <typename CodeUnitType>
	inline VspStringWrapper<CodeUnitType> operator+(const VspStringWrapper<CodeUnitType>& Left, CodeUnitType RightUnit)
	{
		VspStringWrapper<CodeUnitType> Result(Left);
		Result.Append(&RightUnit, 1);
		return Result;
	}

	template <typename CodeUnitType>
	inline VspStringWrapper<CodeUnitType> operator+(CodeUnitType LeftUnit, const VspStringWrapper<CodeUnitType>& Right)
	{
		VspStringWrapper<CodeUnitType> Result(LeftUnit);
		Result.Append(Right);
		return Result;
	}
}
