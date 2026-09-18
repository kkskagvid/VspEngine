#pragma once

#include "Core/Core.h"
#include "Core/String/VspString.h"
#include "Core/Templates/ArrayList.h"

namespace Vsp
{
	// -------------------------------------------------------------------------
	// JsonValue / JsonReader
	// -------------------------------------------------------------------------
	// A small read-only JSON reader. The engine needs it for the metadata the
	// tools write next to the assets they build - the shader manifest HLSLCC
	// emits, for example - and it deliberately stays small: it parses one
	// document into a tree of values and reports failure through return values.
	//
	// Member lookup is by name and never fails: asking for a member that is not
	// there returns a null value, and every accessor takes the fallback it should
	// return in that case. Nothing throws and nothing is allocated outside the
	// tree itself.
	// -------------------------------------------------------------------------
#pragma warning(push)
#pragma warning(disable : 4251)   // ArrayList members: header-only template.
	class RUNTIME_API JsonValue
	{
	public:
		enum class Type : uint32
		{
			Null = 0,
			Boolean,
			Number,
			String,
			Array,
			Object,
		};

		JsonValue() = default;

		Type GetType() const { return m_eType; }
		bool IsNull() const { return m_eType == Type::Null; }
		bool IsArray() const { return m_eType == Type::Array; }
		bool IsObject() const { return m_eType == Type::Object; }
		bool IsValid() const { return m_eType != Type::Null; }

		// -------- Scalar access --------
		bool GetBoolean() const { return m_bBoolean; }
		double GetNumber() const { return m_dNumber; }
		float GetFloat() const { return static_cast<float>(m_dNumber); }
		int32 GetInt() const { return static_cast<int32>(m_dNumber); }
		uint32 GetUInt32() const { return static_cast<uint32>(m_dNumber); }
		const VspString& GetString() const { return m_sText; }

		// -------- Array access --------
		uint32 GetElementCount() const { return static_cast<uint32>(m_Elements.GetSize()); }
		const JsonValue& GetElement(uint32 uIndex) const;

		// -------- Object access --------
		uint32 GetMemberCount() const { return static_cast<uint32>(m_MemberNames.GetSize()); }
		const VspString& GetMemberName(uint32 uIndex) const;
		const JsonValue& GetMember(uint32 uIndex) const;

		// Member by name; a missing member yields a null value.
		const JsonValue& operator[](const char* pMemberName) const;

		// Typed member access with a fallback.
		bool GetMemberBool(const char* pMemberName, bool bFallback) const;
		float GetMemberFloat(const char* pMemberName, float fFallback) const;
		int32 GetMemberInt(const char* pMemberName, int32 nFallback) const;
		uint32 GetMemberUInt32(const char* pMemberName, uint32 uFallback) const;
		VspString GetMemberString(const char* pMemberName, const char* pFallback) const;

		// -------- Building (the reader fills the tree with these) --------
		void SetNull();
		void SetBoolean(bool bValue);
		void SetNumber(double dValue);
		void SetString(const VspString& sValue);
		void SetArray();
		void SetObject();
		JsonValue& AddElement();
		JsonValue& AddMember(const VspString& sMemberName);

	private:
		const JsonValue* FindMember(const char* pMemberName) const;

		Type m_eType = Type::Null;
		bool m_bBoolean = false;
		double m_dNumber = 0.0;
		VspString m_sText;

		ArrayList<JsonValue> m_Elements;
		ArrayList<VspString> m_MemberNames;
		ArrayList<JsonValue> m_Members;
	};
#pragma warning(pop)

	// -------------------------------------------------------------------------
	// JsonReader
	// -------------------------------------------------------------------------
	// Parses one JSON document. Returns false (with outErrorText) when the text
	// is not valid JSON; outRoot then holds nothing.
	// -------------------------------------------------------------------------
	class RUNTIME_API JsonReader
	{
	public:
		static bool Parse(const char* pTextUtf8, JsonValue& outRoot, VspString& outErrorText);

		// Reads a whole file and parses it.
		static bool ParseFile(const VspString& sFilePath, JsonValue& outRoot, VspString& outErrorText);
	};
}
