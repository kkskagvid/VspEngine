#pragma once

#include "Classes/Object.h"
#include "Classes/Shader.h"
#include "Core/Core.h"
#include "Core/String/VspString.h"
#include "Core/Templates/ArrayList.h"

namespace Vsp
{
	// -------------------------------------------------------------------------
	// Material
	// -------------------------------------------------------------------------
	// Native storage of a material: the shader it draws with, the values it gives
	// that shader's properties, and the keywords it turns on. A material is what
	// a renderable component points at, so one shader can be drawn many different
	// ways.
	//
	// The values are plain floats and 4-component vectors, which is what the
	// engine's shaders receive through their push-constant block. Managed code
	// reaches a material through the VspEngine.Material reference handle.
	// -------------------------------------------------------------------------
#pragma warning(push)
#pragma warning(disable : 4251)   // ArrayList members: header-only template.
	class RUNTIME_API Material : public NativeObject
	{
	public:
		struct PropertyValue
		{
			char Name[Shader::k_nMaxPropertyNameLength] = {};
			Shader::PropertyType eType = Shader::PropertyType::Float;
			float fValues[4] = {};
		};

		static constexpr uint32 k_nMaxKeywordCount = 16;

		// -------- Shader --------
		NativeObjectHandle GetShaderHandle() const { return m_uShaderHandle; }
		void SetShaderHandle(NativeObjectHandle uShaderHandle) { m_uShaderHandle = uShaderHandle; }

		// Copies the shader's property defaults and clears the keyword selection,
		// so a fresh material draws exactly like the shader's defaults.
		bool InitializeFromShader(const Shader& shader);

		// -------- Property values --------
		uint32 GetPropertyValueCount() const { return static_cast<uint32>(m_PropertyValues.GetSize()); }
		const PropertyValue* GetPropertyValue(uint32 uValueIndex) const;
		const PropertyValue* FindPropertyValue(const char* pPropertyName) const;

		bool SetFloat(const char* pPropertyName, float fValue);
		bool GetFloat(const char* pPropertyName, float& outValue) const;
		bool SetVector(const char* pPropertyName, const float* pValues);
		bool GetVector(const char* pPropertyName, float* outValues) const;

		// -------- Keywords --------
		// Turns one keyword of the shader's keyword groups on or off. Enabling a
		// keyword of a group disables that group's other keywords, because a
		// group's states exclude each other.
		bool SetKeywordEnabled(const char* pKeyword, bool bIsEnabled);
		bool IsKeywordEnabled(const char* pKeyword) const;

		// -------- Variant selection --------
		// Fills outKeywordStateIndices (one entry per keyword group of the shader)
		// with the states this material selects.
		void BuildKeywordStateIndices(const Shader& shader, uint32* outKeywordStateIndices) const;

		// Variant of the given shader this material draws with.
		const Shader::VariantModule* ResolveVariant(const Shader& shader) const;

	private:
		PropertyValue* FindMutablePropertyValue(const char* pPropertyName);
		bool EnsurePropertyValue(const Shader& shader, const char* pPropertyName, PropertyValue*& outPropertyValue);

		NativeObjectHandle m_uShaderHandle = k_nInvalidObjectHandle;
		ArrayList<PropertyValue> m_PropertyValues;
		ArrayList<VspString> m_EnabledKeywords;
	};
#pragma warning(pop)
}
