#include "RuntimePCH.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "Common/PlatformMisc.h"
#include "Core/Json/JsonReader.h"
#include "Core/Logging/Log.h"

namespace Vsp
{
	static constexpr const char* kLogTag = "JsonReader";

	// -------------------------------------------------------------------------
	// JsonValue
	// -------------------------------------------------------------------------

	void JsonValue::SetNull()
	{
		m_eType = Type::Null;
		m_bBoolean = false;
		m_dNumber = 0.0;
		m_sText = nullptr;
	}

	void JsonValue::SetBoolean(bool bValue)
	{
		SetNull();
		m_eType = Type::Boolean;
		m_bBoolean = bValue;
	}

	void JsonValue::SetNumber(double dValue)
	{
		SetNull();
		m_eType = Type::Number;
		m_dNumber = dValue;
	}

	void JsonValue::SetString(const VspString& sValue)
	{
		SetNull();
		m_eType = Type::String;
		m_sText = sValue;
	}

	void JsonValue::SetArray()
	{
		SetNull();
		m_eType = Type::Array;
		m_Elements.Clear();
	}

	void JsonValue::SetObject()
	{
		SetNull();
		m_eType = Type::Object;
		m_MemberNames.Clear();
		m_Members.Clear();
	}

	JsonValue& JsonValue::AddElement()
	{
		m_eType = Type::Array;
		return m_Elements.Add(JsonValue());
	}

	JsonValue& JsonValue::AddMember(const VspString& sMemberName)
	{
		m_eType = Type::Object;
		m_MemberNames.Add(sMemberName);
		return m_Members.Add(JsonValue());
	}

	const JsonValue& JsonValue::GetElement(uint32 uIndex) const
	{
		static const JsonValue s_NullValue;
		return (uIndex < m_Elements.GetSize()) ? m_Elements[uIndex] : s_NullValue;
	}

	const VspString& JsonValue::GetMemberName(uint32 uIndex) const
	{
		static const VspString s_EmptyName;
		return (uIndex < m_MemberNames.GetSize()) ? m_MemberNames[uIndex] : s_EmptyName;
	}

	const JsonValue& JsonValue::GetMember(uint32 uIndex) const
	{
		static const JsonValue s_NullValue;
		return (uIndex < m_Members.GetSize()) ? m_Members[uIndex] : s_NullValue;
	}

	const JsonValue* JsonValue::FindMember(const char* pMemberName) const
	{
		if (pMemberName == nullptr)
		{
			return nullptr;
		}

		for (size_t nMemberIndex = 0; nMemberIndex < m_MemberNames.GetSize(); ++nMemberIndex)
		{
			if (m_MemberNames[nMemberIndex].Equals(pMemberName))
			{
				return &m_Members[nMemberIndex];
			}
		}
		return nullptr;
	}

	const JsonValue& JsonValue::operator[](const char* pMemberName) const
	{
		static const JsonValue s_NullValue;
		const JsonValue* pMember = FindMember(pMemberName);
		return (pMember != nullptr) ? *pMember : s_NullValue;
	}

	bool JsonValue::GetMemberBool(const char* pMemberName, bool bFallback) const
	{
		const JsonValue* pMember = FindMember(pMemberName);
		if (pMember == nullptr)
		{
			return bFallback;
		}
		if (pMember->m_eType == Type::Boolean)
		{
			return pMember->m_bBoolean;
		}
		if (pMember->m_eType == Type::Number)
		{
			return pMember->m_dNumber != 0.0;
		}
		return bFallback;
	}

	float JsonValue::GetMemberFloat(const char* pMemberName, float fFallback) const
	{
		const JsonValue* pMember = FindMember(pMemberName);
		return (pMember != nullptr && pMember->m_eType == Type::Number) ? pMember->GetFloat() : fFallback;
	}

	int32 JsonValue::GetMemberInt(const char* pMemberName, int32 nFallback) const
	{
		const JsonValue* pMember = FindMember(pMemberName);
		return (pMember != nullptr && pMember->m_eType == Type::Number) ? pMember->GetInt() : nFallback;
	}

	uint32 JsonValue::GetMemberUInt32(const char* pMemberName, uint32 uFallback) const
	{
		const JsonValue* pMember = FindMember(pMemberName);
		if (pMember == nullptr || pMember->m_eType != Type::Number)
		{
			return uFallback;
		}
		return (pMember->m_dNumber >= 0.0) ? pMember->GetUInt32() : uFallback;
	}

	VspString JsonValue::GetMemberString(const char* pMemberName, const char* pFallback) const
	{
		const JsonValue* pMember = FindMember(pMemberName);
		return (pMember != nullptr && pMember->m_eType == Type::String)
			? pMember->m_sText
			: VspString(pFallback);
	}

	// -------------------------------------------------------------------------
	// JsonReader
	// -------------------------------------------------------------------------

	namespace
	{
		// One recursive-descent pass over the document.
		class JsonParser
		{
		public:
			JsonParser(const char* pTextUtf8, JsonValue& outRoot)
				: m_pCursor(pTextUtf8)
				, m_Root(outRoot)
			{
			}

			bool Parse(std::string& outErrorText);

		private:
			void SkipWhitespace();
			bool ParseValue(JsonValue& outValue, uint32 uDepth);
			bool ParseObject(JsonValue& outValue, uint32 uDepth);
			bool ParseArray(JsonValue& outValue, uint32 uDepth);
			bool ParseString(VspString& outText);
			bool ParseNumber(JsonValue& outValue);
			bool ParseLiteral(const char* pLiteral);
			bool Fail(const char* pMessage, std::string& outErrorText);

			// Byte offset of the cursor inside the document, for error messages.
			size_t GetCursorOffset() const { return static_cast<size_t>(m_pCursor - m_pDocumentStart); }

			const char* m_pDocumentStart = nullptr;
			const char* m_pCursor = nullptr;
			JsonValue& m_Root;
		};

		constexpr uint32 k_nMaxJsonDepth = 32;

		bool JsonParser::Fail(const char* pMessage, std::string& outErrorText)
		{
			char sBuffer[64] = {};
			snprintf(sBuffer, sizeof(sBuffer), " at byte %zu", GetCursorOffset());
			outErrorText = std::string(pMessage) + sBuffer;
			return false;
		}

		void JsonParser::SkipWhitespace()
		{
			while (*m_pCursor == ' ' || *m_pCursor == '\t' || *m_pCursor == '\r' || *m_pCursor == '\n')
			{
				++m_pCursor;
			}
		}

		bool JsonParser::ParseLiteral(const char* pLiteral)
		{
			const size_t nLength = std::strlen(pLiteral);
			if (std::strncmp(m_pCursor, pLiteral, nLength) != 0)
			{
				return false;
			}
			m_pCursor += nLength;
			return true;
		}

		bool JsonParser::ParseString(VspString& outText)
		{
			if (*m_pCursor != '"')
			{
				return false;
			}
			++m_pCursor;

			std::string sText;
			while (*m_pCursor != '\0' && *m_pCursor != '"')
			{
				if (*m_pCursor == '\\')
				{
					++m_pCursor;
					switch (*m_pCursor)
					{
					case '"':  sText.push_back('"'); break;
					case '\\': sText.push_back('\\'); break;
					case '/':  sText.push_back('/'); break;
					case 'b':  sText.push_back('\b'); break;
					case 'f':  sText.push_back('\f'); break;
					case 'n':  sText.push_back('\n'); break;
					case 'r':  sText.push_back('\r'); break;
					case 't':  sText.push_back('\t'); break;
					case '\0': return false;
					default:
						// \uXXXX: the engine's documents are ASCII, so the escape
						// is decoded as its low byte and anything else is dropped.
						if (*m_pCursor == 'u')
						{
							char sCode[5] = {};
							for (uint32 uDigitIndex = 0; uDigitIndex < 4; ++uDigitIndex)
							{
								sCode[uDigitIndex] = m_pCursor[1 + uDigitIndex];
							}
							sText.push_back(static_cast<char>(std::strtoul(sCode, nullptr, 16) & 0xFF));
							m_pCursor += 4;
						}
						break;
					}
					++m_pCursor;
					continue;
				}

				sText.push_back(*m_pCursor);
				++m_pCursor;
			}

			if (*m_pCursor != '"')
			{
				return false;
			}
			++m_pCursor;

			outText = VspString(sText);
			return true;
		}

		bool JsonParser::ParseNumber(JsonValue& outValue)
		{
			const char* pNumberStart = m_pCursor;
			if (*m_pCursor == '-' || *m_pCursor == '+')
			{
				++m_pCursor;
			}
			while ((*m_pCursor >= '0' && *m_pCursor <= '9') || *m_pCursor == '.' ||
				*m_pCursor == 'e' || *m_pCursor == 'E' || *m_pCursor == '+' || *m_pCursor == '-')
			{
				++m_pCursor;
			}

			if (m_pCursor == pNumberStart)
			{
				return false;
			}

			outValue.SetNumber(std::atof(std::string(pNumberStart, m_pCursor - pNumberStart).c_str()));
			return true;
		}

		bool JsonParser::ParseObject(JsonValue& outValue, uint32 uDepth)
		{
			outValue.SetObject();
			++m_pCursor;   // '{'
			SkipWhitespace();

			if (*m_pCursor == '}')
			{
				++m_pCursor;
				return true;
			}

			while (true)
			{
				SkipWhitespace();

				VspString sMemberName;
				if (!ParseString(sMemberName))
				{
					return false;
				}

				SkipWhitespace();
				if (*m_pCursor != ':')
				{
					return false;
				}
				++m_pCursor;
				SkipWhitespace();

				JsonValue& member = outValue.AddMember(sMemberName);
				if (!ParseValue(member, uDepth + 1))
				{
					return false;
				}

				SkipWhitespace();
				if (*m_pCursor == ',')
				{
					++m_pCursor;
					continue;
				}
				if (*m_pCursor == '}')
				{
					++m_pCursor;
					return true;
				}
				return false;
			}
		}

		bool JsonParser::ParseArray(JsonValue& outValue, uint32 uDepth)
		{
			outValue.SetArray();
			++m_pCursor;   // '['
			SkipWhitespace();

			if (*m_pCursor == ']')
			{
				++m_pCursor;
				return true;
			}

			while (true)
			{
				SkipWhitespace();

				JsonValue& element = outValue.AddElement();
				if (!ParseValue(element, uDepth + 1))
				{
					return false;
				}

				SkipWhitespace();
				if (*m_pCursor == ',')
				{
					++m_pCursor;
					continue;
				}
				if (*m_pCursor == ']')
				{
					++m_pCursor;
					return true;
				}
				return false;
			}
		}

		bool JsonParser::ParseValue(JsonValue& outValue, uint32 uDepth)
		{
			if (uDepth > k_nMaxJsonDepth)
			{
				return false;
			}

			SkipWhitespace();

			switch (*m_pCursor)
			{
			case '{': return ParseObject(outValue, uDepth);
			case '[': return ParseArray(outValue, uDepth);

			case '"':
			{
				VspString sText;
				if (!ParseString(sText))
				{
					return false;
				}
				outValue.SetString(sText);
				return true;
			}

			case 't':
				if (!ParseLiteral("true")) return false;
				outValue.SetBoolean(true);
				return true;

			case 'f':
				if (!ParseLiteral("false")) return false;
				outValue.SetBoolean(false);
				return true;

			case 'n':
				if (!ParseLiteral("null")) return false;
				outValue.SetNull();
				return true;

			default:
				return ParseNumber(outValue);
			}
		}

		bool JsonParser::Parse(std::string& outErrorText)
		{
			m_pDocumentStart = m_pCursor;
			m_Root.SetNull();

			if (!ParseValue(m_Root, 0))
			{
				return Fail("the document is not valid JSON", outErrorText);
			}

			SkipWhitespace();
			if (*m_pCursor != '\0')
			{
				return Fail("unexpected text after the document", outErrorText);
			}
			return true;
		}
	}

	bool JsonReader::Parse(const char* pTextUtf8, JsonValue& outRoot, VspString& outErrorText)
	{
		outErrorText = nullptr;
		if (pTextUtf8 == nullptr)
		{
			outErrorText = "the JSON text is null";
			return false;
		}

		JsonParser parser(pTextUtf8, outRoot);
		std::string sErrorText;
		if (!parser.Parse(sErrorText))
		{
			outErrorText = VspString(sErrorText);
			return false;
		}
		return true;
	}

	bool JsonReader::ParseFile(const VspString& sFilePath, JsonValue& outRoot, VspString& outErrorText)
	{
		outErrorText = nullptr;

		FILE* pFile = PlatformMisc::OpenFileForReading(sFilePath);
		if (pFile == nullptr)
		{
			outErrorText = "cannot read the JSON file '" + sFilePath + "'";
			return false;
		}

		std::string sText;
		char sBuffer[8192];
		size_t nReadByteCount = 0;
		while ((nReadByteCount = fread(sBuffer, 1, sizeof(sBuffer), pFile)) > 0)
		{
			sText.append(sBuffer, nReadByteCount);
		}
		fclose(pFile);

		if (!Parse(sText.c_str(), outRoot, outErrorText))
		{
			LOG_ERROR(kLogTag, "'{}': {}", sFilePath.GetData(), outErrorText.GetData());
			return false;
		}
		return true;
	}
}
