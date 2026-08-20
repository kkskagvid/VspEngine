#include "Core/RuntimePCH.h"

#include "Core/String/VspString.h"

#include <cstdlib>
#include <cstring>
#include <utility>

namespace
{
	// Code point written in place of malformed input sequences.
	constexpr char32_t k_uReplacementCodePoint = 0xFFFD;

	// FNV-1a hash parameters.
	constexpr uint64_t k_uFnv1aOffsetBasis = 14695981039346656037ULL;
	constexpr uint64_t k_uFnv1aPrime = 1099511628211ULL;
}

namespace Vsp
{
	// =========================================================================
	// Construction
	// =========================================================================

	VspString::VspString() {}

	VspString::VspString(std::nullptr_t) {}

	VspString::VspString(const char* pUtf8Text)
	{
		// VspStringWrapper::Append ignores null pointers.
		m_Text.Append(pUtf8Text);
	}

	VspString::VspString(const char8_t* pUtf8Text)
	{
		if (pUtf8Text != nullptr)
		{
			m_Text.Append(reinterpret_cast<const char*>(pUtf8Text));
		}
	}

	VspString::VspString(const char* pUtf8Bytes, SizeType nByteCount)
	{
		m_Text.Append(pUtf8Bytes, nByteCount);
	}

	VspString::VspString(const char16_t* pUtf16Text)
		: VspString(FromUtf16(pUtf16Text))
	{
	}

	VspString::VspString(const char32_t* pUtf32Text)
		: VspString(FromUtf32(pUtf32Text))
	{
	}

	VspString::VspString(const wchar_t* pWideText)
		: VspString(FromWideText(pWideText))
	{
	}

	VspString::VspString(const std::string& Utf8String)
	{
		m_Text.Append(Utf8String.data(), Utf8String.size());
	}

	VspString::VspString(CodePointType uCodePoint)
	{
		char szEncodedCodePoint[4];
		SizeType nEncodedByteCount = EncodeCodePointToUtf8(uCodePoint, szEncodedCodePoint);
		m_Text.Append(szEncodedCodePoint, nEncodedByteCount);
	}

	VspString::VspString(const VspString& Other)
		: m_Text(Other.m_Text)
	{
	}

	VspString::VspString(VspString&& Other)
		: m_Text(std::move(Other.m_Text))
	{
	}

	VspString::~VspString() {}

	// =========================================================================
	// Static factories
	// =========================================================================

	VspString VspString::FromUtf16(const char16_t* pUtf16Text)
	{
		if (pUtf16Text == nullptr)
		{
			return VspString();
		}

		SizeType nUnitCount = 0;
		while (pUtf16Text[nUnitCount] != char16_t(0))
		{
			++nUnitCount;
		}
		return FromUtf16(pUtf16Text, nUnitCount);
	}

	VspString VspString::FromUtf16(const char16_t* pUtf16Units, SizeType nUnitCount)
	{
		VspString Result;
		if (pUtf16Units == nullptr || nUnitCount == 0)
		{
			return Result;
		}

		// Transcode into a temporary byte buffer, then copy it into the storage.
		SizeType nRequiredByteCount = ConvertUtf16ToUtf8(pUtf16Units, nUnitCount, nullptr, 0);
		char* pUtf8Buffer = static_cast<char*>(malloc(nRequiredByteCount));
		if (pUtf8Buffer == nullptr)
		{
			DEBUG_BREAK();
			return Result;
		}
		ConvertUtf16ToUtf8(pUtf16Units, nUnitCount, pUtf8Buffer, nRequiredByteCount);
		Result.m_Text.Append(pUtf8Buffer, nRequiredByteCount);
		free(pUtf8Buffer);
		return Result;
	}

	VspString VspString::FromUtf32(const char32_t* pUtf32Text)
	{
		if (pUtf32Text == nullptr)
		{
			return VspString();
		}

		SizeType nUnitCount = 0;
		while (pUtf32Text[nUnitCount] != char32_t(0))
		{
			++nUnitCount;
		}
		return FromUtf32(pUtf32Text, nUnitCount);
	}

	VspString VspString::FromUtf32(const char32_t* pUtf32Units, SizeType nUnitCount)
	{
		VspString Result;
		if (pUtf32Units == nullptr || nUnitCount == 0)
		{
			return Result;
		}

		// Transcode into a temporary byte buffer, then copy it into the storage.
		SizeType nRequiredByteCount = ConvertUtf32ToUtf8(pUtf32Units, nUnitCount, nullptr, 0);
		char* pUtf8Buffer = static_cast<char*>(malloc(nRequiredByteCount));
		if (pUtf8Buffer == nullptr)
		{
			DEBUG_BREAK();
			return Result;
		}
		ConvertUtf32ToUtf8(pUtf32Units, nUnitCount, pUtf8Buffer, nRequiredByteCount);
		Result.m_Text.Append(pUtf8Buffer, nRequiredByteCount);
		free(pUtf8Buffer);
		return Result;
	}

	VspString VspString::FromWideText(const wchar_t* pWideText)
	{
		if (pWideText == nullptr)
		{
			return VspString();
		}

		SizeType nUnitCount = 0;
		while (pWideText[nUnitCount] != L'\0')
		{
			++nUnitCount;
		}
		return FromWideText(pWideText, nUnitCount);
	}

	VspString VspString::FromWideText(const wchar_t* pWideUnits, SizeType nWideUnitCount)
	{
		if constexpr (sizeof(wchar_t) == sizeof(char16_t))
		{
			return FromUtf16(reinterpret_cast<const char16_t*>(pWideUnits), nWideUnitCount);
		}
		else
		{
			return FromUtf32(reinterpret_cast<const char32_t*>(pWideUnits), nWideUnitCount);
		}
	}

	// =========================================================================
	// Assignment
	// =========================================================================

	VspString& VspString::operator=(const VspString& Other)
	{
		m_Text = Other.m_Text;
		return *this;
	}

	VspString& VspString::operator=(VspString&& Other)
	{
		m_Text = std::move(Other.m_Text);
		return *this;
	}

	VspString& VspString::operator=(std::nullptr_t)
	{
		m_Text = nullptr;
		return *this;
	}

	VspString& VspString::operator=(const char* pUtf8Text)
	{
		m_Text = pUtf8Text;
		return *this;
	}

	VspString& VspString::operator=(const std::string& Utf8String)
	{
		m_Text.Clear();
		m_Text.Append(Utf8String.data(), Utf8String.size());
		return *this;
	}

	// =========================================================================
	// Conversion to other encodings
	// =========================================================================

	ArrayList<char16_t> VspString::ToUtf16() const
	{
		ArrayList<char16_t> Result;
		Result.Reserve(GetCodePointCount() + 1);

		const char* pCursor = m_Text.GetData();
		const char* pEnd = pCursor + m_Text.GetLength();
		CodePointType uCodePoint = 0;
		while (pCursor < pEnd)
		{
			DecodeUtf8CodePoint(pCursor, pEnd, uCodePoint);
			char16_t szEncodedUnits[2];
			SizeType nEncodedUnitCount = EncodeCodePointToUtf16(uCodePoint, szEncodedUnits);
			for (SizeType nUnitIndex = 0; nUnitIndex < nEncodedUnitCount; ++nUnitIndex)
			{
				Result.Add(szEncodedUnits[nUnitIndex]);
			}
		}
		Result.Add(char16_t(0));
		return Result;
	}

	ArrayList<char32_t> VspString::ToUtf32() const
	{
		ArrayList<char32_t> Result;
		Result.Reserve(GetCodePointCount() + 1);

		const char* pCursor = m_Text.GetData();
		const char* pEnd = pCursor + m_Text.GetLength();
		CodePointType uCodePoint = 0;
		while (pCursor < pEnd)
		{
			DecodeUtf8CodePoint(pCursor, pEnd, uCodePoint);
			Result.Add(SanitizeCodePoint(uCodePoint));
		}
		Result.Add(char32_t(0));
		return Result;
	}

	ArrayList<wchar_t> VspString::ToWideText() const
	{
		if constexpr (sizeof(wchar_t) == sizeof(char16_t))
		{
			ArrayList<char16_t> Utf16Units = ToUtf16();
			ArrayList<wchar_t> Result;
			Result.Reserve(Utf16Units.GetSize());
			for (SizeType nUnitIndex = 0; nUnitIndex < Utf16Units.GetSize(); ++nUnitIndex)
			{
				Result.Add(static_cast<wchar_t>(Utf16Units[nUnitIndex]));
			}
			return Result;
		}
		else
		{
			ArrayList<char32_t> Utf32Units = ToUtf32();
			ArrayList<wchar_t> Result;
			Result.Reserve(Utf32Units.GetSize());
			for (SizeType nUnitIndex = 0; nUnitIndex < Utf32Units.GetSize(); ++nUnitIndex)
			{
				Result.Add(static_cast<wchar_t>(Utf32Units[nUnitIndex]));
			}
			return Result;
		}
	}

	std::string VspString::ToStdString() const
	{
		return std::string(m_Text.GetData(), m_Text.GetLength());
	}

	// =========================================================================
	// Raw transcoding
	// =========================================================================

	VspString::SizeType VspString::ConvertUtf8ToUtf16(const char* pUtf8Bytes, SizeType nUtf8ByteCount, char16_t* pOutUtf16Units, SizeType nOutCapacity)
	{
		if (pUtf8Bytes == nullptr)
		{
			return 0;
		}

		const char* pCursor = pUtf8Bytes;
		const char* pEnd = pUtf8Bytes + nUtf8ByteCount;
		SizeType nWrittenUnitCount = 0;
		CodePointType uCodePoint = 0;
		while (pCursor < pEnd)
		{
			DecodeUtf8CodePoint(pCursor, pEnd, uCodePoint);
			char16_t szEncodedUnits[2];
			SizeType nEncodedUnitCount = EncodeCodePointToUtf16(uCodePoint, szEncodedUnits);
			if (pOutUtf16Units != nullptr && nWrittenUnitCount + nEncodedUnitCount <= nOutCapacity)
			{
				memcpy(pOutUtf16Units + nWrittenUnitCount, szEncodedUnits, nEncodedUnitCount * sizeof(char16_t));
			}
			nWrittenUnitCount += nEncodedUnitCount;
		}
		return nWrittenUnitCount;
	}

	VspString::SizeType VspString::ConvertUtf8ToUtf32(const char* pUtf8Bytes, SizeType nUtf8ByteCount, char32_t* pOutUtf32Units, SizeType nOutCapacity)
	{
		if (pUtf8Bytes == nullptr)
		{
			return 0;
		}

		const char* pCursor = pUtf8Bytes;
		const char* pEnd = pUtf8Bytes + nUtf8ByteCount;
		SizeType nWrittenUnitCount = 0;
		CodePointType uCodePoint = 0;
		while (pCursor < pEnd)
		{
			DecodeUtf8CodePoint(pCursor, pEnd, uCodePoint);
			if (pOutUtf32Units != nullptr && nWrittenUnitCount < nOutCapacity)
			{
				pOutUtf32Units[nWrittenUnitCount] = uCodePoint;
			}
			++nWrittenUnitCount;
		}
		return nWrittenUnitCount;
	}

	VspString::SizeType VspString::ConvertUtf16ToUtf8(const char16_t* pUtf16Units, SizeType nUtf16UnitCount, char* pOutUtf8Bytes, SizeType nOutCapacity)
	{
		if (pUtf16Units == nullptr)
		{
			return 0;
		}

		const char16_t* pCursor = pUtf16Units;
		const char16_t* pEnd = pUtf16Units + nUtf16UnitCount;
		SizeType nWrittenByteCount = 0;
		CodePointType uCodePoint = 0;
		while (pCursor < pEnd)
		{
			DecodeUtf16CodePoint(pCursor, pEnd, uCodePoint);
			char szEncodedBytes[4];
			SizeType nEncodedByteCount = EncodeCodePointToUtf8(uCodePoint, szEncodedBytes);
			if (pOutUtf8Bytes != nullptr && nWrittenByteCount + nEncodedByteCount <= nOutCapacity)
			{
				memcpy(pOutUtf8Bytes + nWrittenByteCount, szEncodedBytes, nEncodedByteCount);
			}
			nWrittenByteCount += nEncodedByteCount;
		}
		return nWrittenByteCount;
	}

	VspString::SizeType VspString::ConvertUtf32ToUtf8(const char32_t* pUtf32Units, SizeType nUtf32UnitCount, char* pOutUtf8Bytes, SizeType nOutCapacity)
	{
		if (pUtf32Units == nullptr)
		{
			return 0;
		}

		SizeType nWrittenByteCount = 0;
		for (SizeType nUnitIndex = 0; nUnitIndex < nUtf32UnitCount; ++nUnitIndex)
		{
			char szEncodedBytes[4];
			SizeType nEncodedByteCount = EncodeCodePointToUtf8(pUtf32Units[nUnitIndex], szEncodedBytes);
			if (pOutUtf8Bytes != nullptr && nWrittenByteCount + nEncodedByteCount <= nOutCapacity)
			{
				memcpy(pOutUtf8Bytes + nWrittenByteCount, szEncodedBytes, nEncodedByteCount);
			}
			nWrittenByteCount += nEncodedByteCount;
		}
		return nWrittenByteCount;
	}

	VspString::SizeType VspString::ConvertUtf16ToUtf32(const char16_t* pUtf16Units, SizeType nUtf16UnitCount, char32_t* pOutUtf32Units, SizeType nOutCapacity)
	{
		if (pUtf16Units == nullptr)
		{
			return 0;
		}

		const char16_t* pCursor = pUtf16Units;
		const char16_t* pEnd = pUtf16Units + nUtf16UnitCount;
		SizeType nWrittenUnitCount = 0;
		CodePointType uCodePoint = 0;
		while (pCursor < pEnd)
		{
			DecodeUtf16CodePoint(pCursor, pEnd, uCodePoint);
			if (pOutUtf32Units != nullptr && nWrittenUnitCount < nOutCapacity)
			{
				pOutUtf32Units[nWrittenUnitCount] = uCodePoint;
			}
			++nWrittenUnitCount;
		}
		return nWrittenUnitCount;
	}

	VspString::SizeType VspString::ConvertUtf32ToUtf16(const char32_t* pUtf32Units, SizeType nUtf32UnitCount, char16_t* pOutUtf16Units, SizeType nOutCapacity)
	{
		if (pUtf32Units == nullptr)
		{
			return 0;
		}

		SizeType nWrittenUnitCount = 0;
		for (SizeType nUnitIndex = 0; nUnitIndex < nUtf32UnitCount; ++nUnitIndex)
		{
			char16_t szEncodedUnits[2];
			SizeType nEncodedUnitCount = EncodeCodePointToUtf16(pUtf32Units[nUnitIndex], szEncodedUnits);
			if (pOutUtf16Units != nullptr && nWrittenUnitCount + nEncodedUnitCount <= nOutCapacity)
			{
				memcpy(pOutUtf16Units + nWrittenUnitCount, szEncodedUnits, nEncodedUnitCount * sizeof(char16_t));
			}
			nWrittenUnitCount += nEncodedUnitCount;
		}
		return nWrittenUnitCount;
	}

	// =========================================================================
	// Validation
	// =========================================================================

	bool VspString::IsValidUtf8() const
	{
		return IsValidUtf8(m_Text.GetData(), m_Text.GetLength());
	}

	bool VspString::IsValidUtf8(const char* pUtf8Bytes, SizeType nUtf8ByteCount)
	{
		if (pUtf8Bytes == nullptr && nUtf8ByteCount > 0)
		{
			return false;
		}

		const char* pCursor = pUtf8Bytes;
		const char* pEnd = pUtf8Bytes != nullptr ? pUtf8Bytes + nUtf8ByteCount : nullptr;
		CodePointType uCodePoint = 0;
		while (pCursor < pEnd)
		{
			if (!DecodeUtf8CodePoint(pCursor, pEnd, uCodePoint))
			{
				return false;
			}
		}
		return true;
	}

	// =========================================================================
	// Observers
	// =========================================================================

	VspString::SizeType VspString::GetByteLength() const
	{
		return m_Text.GetLength();
	}

	VspString::SizeType VspString::GetCodePointCount() const
	{
		SizeType nCodePointCount = 0;
		const char* pCursor = m_Text.GetData();
		const char* pEnd = pCursor + m_Text.GetLength();
		CodePointType uCodePoint = 0;
		while (pCursor < pEnd)
		{
			DecodeUtf8CodePoint(pCursor, pEnd, uCodePoint);
			++nCodePointCount;
		}
		return nCodePointCount;
	}

	bool VspString::IsEmpty() const
	{
		return m_Text.IsEmpty();
	}

	const char* VspString::GetData() const
	{
		return m_Text.GetData();
	}

	// =========================================================================
	// Code point access
	// =========================================================================

	VspString::CodePointType VspString::GetCodePointAt(SizeType nCodePointIndex) const
	{
		if (m_Text.IsEmpty())
		{
			DEBUG_BREAK();
			return 0;
		}

		SizeType nCurrentIndex = 0;
		const char* pCursor = m_Text.GetData();
		const char* pEnd = pCursor + m_Text.GetLength();
		CodePointType uCodePoint = 0;
		while (pCursor < pEnd)
		{
			DecodeUtf8CodePoint(pCursor, pEnd, uCodePoint);
			if (nCurrentIndex == nCodePointIndex)
			{
				return uCodePoint;
			}
			++nCurrentIndex;
		}

		// Out of range: clamp to the last decoded code point.
		DEBUG_BREAK();
		return uCodePoint;
	}

	VspString::CodePointType VspString::GetFirstCodePoint() const
	{
		return GetCodePointAt(0);
	}

	VspString::CodePointType VspString::GetLastCodePoint() const
	{
		if (m_Text.IsEmpty())
		{
			DEBUG_BREAK();
			return 0;
		}

		const char* pCursor = m_Text.GetData();
		const char* pEnd = pCursor + m_Text.GetLength();
		CodePointType uCodePoint = 0;
		while (pCursor < pEnd)
		{
			DecodeUtf8CodePoint(pCursor, pEnd, uCodePoint);
		}
		return uCodePoint;
	}

	VspString::SizeType VspString::GetCodePointByteOffset(SizeType nCodePointIndex) const
	{
		if (m_Text.IsEmpty())
		{
			return 0;
		}

		SizeType nCurrentIndex = 0;
		const char* pCursor = m_Text.GetData();
		const char* pEnd = pCursor + m_Text.GetLength();
		CodePointType uCodePoint = 0;
		while (pCursor < pEnd)
		{
			if (nCurrentIndex == nCodePointIndex)
			{
				return static_cast<SizeType>(pCursor - m_Text.GetData());
			}
			DecodeUtf8CodePoint(pCursor, pEnd, uCodePoint);
			++nCurrentIndex;
		}

		// Index at or past the end: report the append position.
		return m_Text.GetLength();
	}

	// =========================================================================
	// Modification
	// =========================================================================

	VspString& VspString::Append(const VspString& Other)
	{
		m_Text.Append(Other.m_Text);
		return *this;
	}

	VspString& VspString::Append(const std::string& Utf8String)
	{
		m_Text.Append(Utf8String.data(), Utf8String.size());
		return *this;
	}

	VspString& VspString::Append(const char* pUtf8Text)
	{
		m_Text.Append(pUtf8Text);
		return *this;
	}

	VspString& VspString::Append(const char8_t* pUtf8Text)
	{
		if (pUtf8Text != nullptr)
		{
			m_Text.Append(reinterpret_cast<const char*>(pUtf8Text));
		}
		return *this;
	}

	VspString& VspString::Append(const char* pUtf8Bytes, SizeType nByteCount)
	{
		m_Text.Append(pUtf8Bytes, nByteCount);
		return *this;
	}

	VspString& VspString::AppendCodePoint(CodePointType uCodePoint)
	{
		m_Text.AppendCodePoint(uCodePoint);
		return *this;
	}

	VspString& VspString::InsertCodePoint(SizeType nCodePointIndex, CodePointType uCodePoint)
	{
		// An index at or past the end simply appends.
		SizeType nByteOffset = GetCodePointByteOffset(nCodePointIndex);
		SizeType nByteLength = m_Text.GetLength();

		// Rebuild: head bytes + encoded code point + tail bytes.
		VspString Result;
		Result.m_Text.Append(m_Text.GetData(), nByteOffset);
		Result.AppendCodePoint(uCodePoint);
		Result.m_Text.Append(m_Text.GetData() + nByteOffset, nByteLength - nByteOffset);
		m_Text = std::move(Result.m_Text);
		return *this;
	}

	VspString& VspString::RemoveCodePointAt(SizeType nCodePointIndex)
	{
		return RemoveCodePointRange(nCodePointIndex, 1);
	}

	VspString& VspString::RemoveCodePointRange(SizeType nFirstCodePointIndex, SizeType nCodePointCount)
	{
		if (nFirstCodePointIndex >= GetCodePointCount())
		{
			DEBUG_BREAK();
			return *this;
		}

		SizeType nStartByteOffset = GetCodePointByteOffset(nFirstCodePointIndex);
		SizeType nEndByteOffset = nStartByteOffset;

		const char* pCursor = m_Text.GetData() + nStartByteOffset;
		const char* pEnd = m_Text.GetData() + m_Text.GetLength();
		CodePointType uCodePoint = 0;
		for (SizeType nRemovedIndex = 0; nRemovedIndex < nCodePointCount && pCursor < pEnd; ++nRemovedIndex)
		{
			DecodeUtf8CodePoint(pCursor, pEnd, uCodePoint);
			nEndByteOffset = static_cast<SizeType>(pCursor - m_Text.GetData());
		}

		SizeType nRemovedByteCount = nEndByteOffset - nStartByteOffset;
		if (nRemovedByteCount > 0)
		{
			// Rebuild without the removed byte range.
			VspString Result;
			Result.m_Text.Append(m_Text.GetData(), nStartByteOffset);
			Result.m_Text.Append(m_Text.GetData() + nEndByteOffset, m_Text.GetLength() - nEndByteOffset);
			m_Text = std::move(Result.m_Text);
		}
		return *this;
	}

	VspString& VspString::Clear()
	{
		m_Text.Clear();
		return *this;
	}

	VspString& VspString::Reserve(SizeType nByteCapacity)
	{
		m_Text.Reserve(nByteCapacity);
		return *this;
	}

	VspString& VspString::ShrinkToFit()
	{
		// Rebuild the storage so its capacity matches the content exactly.
		m_Text = Utf8String(m_Text.GetData(), m_Text.GetLength());
		return *this;
	}

	VspString VspString::GetSubString(SizeType nStartByteOffset, SizeType nByteCount) const
	{
		if (nStartByteOffset > m_Text.GetLength())
		{
			DEBUG_BREAK();
			nStartByteOffset = m_Text.GetLength();
		}

		SizeType nAvailableByteCount = m_Text.GetLength() - nStartByteOffset;
		SizeType nClampedByteCount = nByteCount < nAvailableByteCount ? nByteCount : nAvailableByteCount;
		if (nClampedByteCount == 0)
		{
			return VspString();
		}
		return VspString(m_Text.GetData() + nStartByteOffset, nClampedByteCount);
	}

	VspString& VspString::operator+=(const VspString& Other)
	{
		return Append(Other);
	}

	VspString& VspString::operator+=(const std::string& Utf8String)
	{
		return Append(Utf8String);
	}

	VspString& VspString::operator+=(const char* pUtf8Text)
	{
		return Append(pUtf8Text);
	}

	VspString& VspString::operator+=(CodePointType uCodePoint)
	{
		return AppendCodePoint(uCodePoint);
	}

	// =========================================================================
	// Comparison
	// =========================================================================

	bool VspString::Equals(const VspString& Other) const
	{
		return m_Text.Equals(Other.m_Text);
	}

	bool VspString::Equals(const char* pUtf8Text) const
	{
		return m_Text.Equals(pUtf8Text);
	}

	bool VspString::Equals(const std::string& Utf8String) const
	{
		if (m_Text.GetLength() != Utf8String.size())
		{
			return false;
		}
		return Utf8String.size() == 0 || memcmp(m_Text.GetData(), Utf8String.data(), Utf8String.size()) == 0;
	}

	int VspString::CompareTo(const VspString& Other) const
	{
		const char* pLeftCursor = m_Text.GetData();
		const char* pLeftEnd = pLeftCursor + m_Text.GetLength();
		const char* pRightCursor = Other.m_Text.GetData();
		const char* pRightEnd = pRightCursor + Other.m_Text.GetLength();

		CodePointType uLeftCodePoint = 0;
		CodePointType uRightCodePoint = 0;
		while (pLeftCursor < pLeftEnd && pRightCursor < pRightEnd)
		{
			DecodeUtf8CodePoint(pLeftCursor, pLeftEnd, uLeftCodePoint);
			DecodeUtf8CodePoint(pRightCursor, pRightEnd, uRightCodePoint);
			if (uLeftCodePoint != uRightCodePoint)
			{
				return uLeftCodePoint < uRightCodePoint ? -1 : 1;
			}
		}

		if (pLeftCursor < pLeftEnd)
		{
			return 1;
		}
		if (pRightCursor < pRightEnd)
		{
			return -1;
		}
		return 0;
	}

	// =========================================================================
	// Search
	// =========================================================================

	VspString::SizeType VspString::Find(const std::string& NeedleUtf8, SizeType nStartByteOffset) const
	{
		SizeType nByteLength = m_Text.GetLength();

		// An empty needle matches at the (clamped) start position.
		if (NeedleUtf8.empty())
		{
			return nStartByteOffset < nByteLength ? nStartByteOffset : nByteLength;
		}

		if (nStartByteOffset > nByteLength)
		{
			DEBUG_BREAK();
			nStartByteOffset = nByteLength;
		}

		if (NeedleUtf8.size() > nByteLength - nStartByteOffset)
		{
			return InvalidIndex;
		}

		// UTF-8 is self-synchronizing, so a plain byte search cannot match at a
		// position that would split a code point.
		for (SizeType nByteIndex = nStartByteOffset; nByteIndex + NeedleUtf8.size() <= nByteLength; ++nByteIndex)
		{
			if (memcmp(m_Text.GetData() + nByteIndex, NeedleUtf8.data(), NeedleUtf8.size()) == 0)
			{
				return nByteIndex;
			}
		}
		return InvalidIndex;
	}

	VspString::SizeType VspString::FindCodePoint(CodePointType uCodePoint, SizeType nStartCodePointIndex) const
	{
		SizeType nCurrentIndex = 0;
		const char* pCursor = m_Text.GetData();
		const char* pEnd = pCursor + m_Text.GetLength();
		CodePointType uDecodedCodePoint = 0;
		while (pCursor < pEnd)
		{
			DecodeUtf8CodePoint(pCursor, pEnd, uDecodedCodePoint);
			if (nCurrentIndex >= nStartCodePointIndex && uDecodedCodePoint == uCodePoint)
			{
				return nCurrentIndex;
			}
			++nCurrentIndex;
		}
		return InvalidIndex;
	}

	bool VspString::Contains(const std::string& NeedleUtf8) const
	{
		return Find(NeedleUtf8) != InvalidIndex;
	}

	bool VspString::Contains(CodePointType uCodePoint) const
	{
		return FindCodePoint(uCodePoint) != InvalidIndex;
	}

	bool VspString::StartsWith(const std::string& PrefixUtf8) const
	{
		return Find(PrefixUtf8, 0) == 0;
	}

	bool VspString::EndsWith(const std::string& SuffixUtf8) const
	{
		if (SuffixUtf8.empty())
		{
			return true;
		}
		if (SuffixUtf8.size() > m_Text.GetLength())
		{
			return false;
		}
		return memcmp(m_Text.GetData() + m_Text.GetLength() - SuffixUtf8.size(), SuffixUtf8.data(), SuffixUtf8.size()) == 0;
	}

	// =========================================================================
	// Iteration / utilities
	// =========================================================================

	void VspString::ForEachCodePoint(const std::function<void(CodePointType)>& Callback) const
	{
		const char* pCursor = m_Text.GetData();
		const char* pEnd = pCursor + m_Text.GetLength();
		CodePointType uCodePoint = 0;
		while (pCursor < pEnd)
		{
			DecodeUtf8CodePoint(pCursor, pEnd, uCodePoint);
			Callback(uCodePoint);
		}
	}

	uint64_t VspString::GetHashCode() const
	{
		uint64_t uHashValue = k_uFnv1aOffsetBasis;
		for (SizeType nByteIndex = 0; nByteIndex < m_Text.GetLength(); ++nByteIndex)
		{
			uHashValue ^= static_cast<unsigned char>(m_Text.GetData()[nByteIndex]);
			uHashValue *= k_uFnv1aPrime;
		}
		return uHashValue;
	}

	// =========================================================================
	// Code point <-> code unit helpers
	// =========================================================================

	VspString::CodePointType VspString::SanitizeCodePoint(CodePointType uCodePoint)
	{
		// Surrogate halves and values above U+10FFFF are not valid code points.
		if (uCodePoint > 0x10FFFF || (uCodePoint >= 0xD800 && uCodePoint <= 0xDFFF))
		{
			return k_uReplacementCodePoint;
		}
		return uCodePoint;
	}

	bool VspString::DecodeUtf8CodePoint(const char*& pCursor, const char* pEnd, CodePointType& uOutCodePoint)
	{
		const char* pSequenceStart = pCursor;
		unsigned char uFirstByte = static_cast<unsigned char>(*pCursor);
		++pCursor;

		// Single-byte sequence (ASCII): no further checks needed.
		if (uFirstByte < 0x80)
		{
			uOutCodePoint = static_cast<CodePointType>(uFirstByte);
			return true;
		}

		// Classify the leading byte to learn the sequence length and the code
		// point below which the encoding would be overlong.
		int nContinuationByteCount = 0;
		CodePointType uCodePoint = 0;
		CodePointType uMinimumCodePoint = 0;
		if ((uFirstByte & 0xE0) == 0xC0)
		{
			nContinuationByteCount = 1;
			uCodePoint = static_cast<CodePointType>(uFirstByte & 0x1F);
			uMinimumCodePoint = 0x80;
		}
		else if ((uFirstByte & 0xF0) == 0xE0)
		{
			nContinuationByteCount = 2;
			uCodePoint = static_cast<CodePointType>(uFirstByte & 0x0F);
			uMinimumCodePoint = 0x800;
		}
		else if ((uFirstByte & 0xF8) == 0xF0)
		{
			nContinuationByteCount = 3;
			uCodePoint = static_cast<CodePointType>(uFirstByte & 0x07);
			uMinimumCodePoint = 0x10000;
		}
		else
		{
			// Invalid leading byte: consume it and report a replacement.
			uOutCodePoint = k_uReplacementCodePoint;
			return false;
		}

		// Truncated sequence: consume the leading byte and report a replacement.
		if (static_cast<SizeType>(pEnd - pCursor) < static_cast<SizeType>(nContinuationByteCount))
		{
			pCursor = pSequenceStart + 1;
			uOutCodePoint = k_uReplacementCodePoint;
			return false;
		}

		for (int nContinuationIndex = 0; nContinuationIndex < nContinuationByteCount; ++nContinuationIndex)
		{
			unsigned char uContinuationByte = static_cast<unsigned char>(*pCursor);
			if ((uContinuationByte & 0xC0) != 0x80)
			{
				// Not a continuation byte: consume the leading byte only; the
				// offending byte is re-examined as a fresh lead on the next call.
				pCursor = pSequenceStart + 1;
				uOutCodePoint = k_uReplacementCodePoint;
				return false;
			}
			uCodePoint = (uCodePoint << 6) | static_cast<CodePointType>(uContinuationByte & 0x3F);
			++pCursor;
		}

		// Reject overlong encodings, surrogate halves and out-of-range values.
		if (uCodePoint < uMinimumCodePoint || uCodePoint > 0x10FFFF ||
			(uCodePoint >= 0xD800 && uCodePoint <= 0xDFFF))
		{
			pCursor = pSequenceStart + 1;
			uOutCodePoint = k_uReplacementCodePoint;
			return false;
		}

		uOutCodePoint = uCodePoint;
		return true;
	}

	bool VspString::DecodeUtf16CodePoint(const char16_t*& pCursor, const char16_t* pEnd, CodePointType& uOutCodePoint)
	{
		CodePointType uFirstUnit = static_cast<CodePointType>(*pCursor);
		++pCursor;

		// High surrogate: must be followed by a low surrogate.
		if (uFirstUnit >= 0xD800 && uFirstUnit <= 0xDBFF)
		{
			if (pCursor < pEnd)
			{
				CodePointType uSecondUnit = static_cast<CodePointType>(*pCursor);
				if (uSecondUnit >= 0xDC00 && uSecondUnit <= 0xDFFF)
				{
					++pCursor;
					uOutCodePoint = 0x10000 + ((uFirstUnit - 0xD800) << 10) + (uSecondUnit - 0xDC00);
					return true;
				}
			}
			uOutCodePoint = k_uReplacementCodePoint;
			return false;   // Unpaired high surrogate.
		}

		// Lone low surrogate.
		if (uFirstUnit >= 0xDC00 && uFirstUnit <= 0xDFFF)
		{
			uOutCodePoint = k_uReplacementCodePoint;
			return false;
		}

		uOutCodePoint = uFirstUnit;
		return true;
	}

	VspString::SizeType VspString::EncodeCodePointToUtf8(CodePointType uCodePoint, char* pOutBytes)
	{
		uCodePoint = SanitizeCodePoint(uCodePoint);

		if (uCodePoint <= 0x7F)
		{
			pOutBytes[0] = static_cast<char>(uCodePoint);
			return 1;
		}
		if (uCodePoint <= 0x7FF)
		{
			pOutBytes[0] = static_cast<char>(0xC0 | (uCodePoint >> 6));
			pOutBytes[1] = static_cast<char>(0x80 | (uCodePoint & 0x3F));
			return 2;
		}
		if (uCodePoint <= 0xFFFF)
		{
			pOutBytes[0] = static_cast<char>(0xE0 | (uCodePoint >> 12));
			pOutBytes[1] = static_cast<char>(0x80 | ((uCodePoint >> 6) & 0x3F));
			pOutBytes[2] = static_cast<char>(0x80 | (uCodePoint & 0x3F));
			return 3;
		}

		pOutBytes[0] = static_cast<char>(0xF0 | (uCodePoint >> 18));
		pOutBytes[1] = static_cast<char>(0x80 | ((uCodePoint >> 12) & 0x3F));
		pOutBytes[2] = static_cast<char>(0x80 | ((uCodePoint >> 6) & 0x3F));
		pOutBytes[3] = static_cast<char>(0x80 | (uCodePoint & 0x3F));
		return 4;
	}

	VspString::SizeType VspString::EncodeCodePointToUtf16(CodePointType uCodePoint, char16_t* pOutUnits)
	{
		uCodePoint = SanitizeCodePoint(uCodePoint);

		if (uCodePoint <= 0xFFFF)
		{
			pOutUnits[0] = static_cast<char16_t>(uCodePoint);
			return 1;
		}

		CodePointType uAdjustedCodePoint = uCodePoint - 0x10000;
		pOutUnits[0] = static_cast<char16_t>(0xD800 + (uAdjustedCodePoint >> 10));
		pOutUnits[1] = static_cast<char16_t>(0xDC00 + (uAdjustedCodePoint & 0x3FF));
		return 2;
	}

	VspString::SizeType VspString::EncodeCodePointToUtf32(CodePointType uCodePoint, char32_t* pOutUnits)
	{
		pOutUnits[0] = SanitizeCodePoint(uCodePoint);
		return 1;
	}
}
