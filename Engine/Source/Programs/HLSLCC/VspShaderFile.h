#pragma once

#include <string>
#include <vector>

#include "Result.h"
#include "ShaderDefinition.h"
#include "ShaderReflection.h"

namespace Hlslcc
{
	// -------------------------------------------------------------------------
	// ShaderFile (.vsf)
	// -------------------------------------------------------------------------
	// A .vsf shader file is the engine's shader asset. It carries everything a
	// shader needs in ONE file:
	//
	//   Properties                  the values the editor shows
	//   {
	//       _Tint ("Tint", Color) = (1, 1, 1, 1)
	//       _ColorMode ("Color Mode", Float) = 3
	//   }
	//
	//   Shader "Vsp/Triangle2D"     settings: the render queue and the shader's
	//   {                           variant keyword groups
	//       Queue = "Geometry"
	//       Variant _TINT_ENABLED
	//   }
	//
	//   Pass                        the shader code itself; HLSL, with #include
	//   {                           allowed and the entry points named by pragma
	//       #pragma vertex PassVertex
	//       #pragma fragment PassFragment
	//       #pragma variant _TINT_ENABLED
	//       #pragma multi_variant_local _FLAT_COLOR
	//       #include "Triangle2DCommon.hlsl"
	//       ... HLSL ...
	//   }
	//
	// The four variant pragmas are:
	//   #pragma variant             ...  strippable, global keyword
	//   #pragma variant_local       ...  strippable, local keyword
	//   #pragma multi_variant       ...  always kept, global keyword
	//   #pragma multi_variant_local ...  always kept, local keyword
	// (see ShaderDefinition.h for what "strippable" means).
	//
	// A file WITHOUT a Pass block is treated as one pass holding the whole text,
	// which keeps plain HLSL shaders (and the engine's runtime compiler) working
	// unchanged.
	// -------------------------------------------------------------------------

	// One Pass block of a shader file.
	struct ShaderPass
	{
		char Name[k_nMaxShaderKeywordLength] = {};
		std::string SourceText;                  // HLSL with the shader pragmas removed.
		std::string VertexEntryPoint;            // "#pragma vertex" value or the default.
		std::string FragmentEntryPoint;          // "#pragma fragment" value or the default.
	};

	class ShaderFile
	{
	public:
		static constexpr const char* k_sDefaultVertexEntryPoint = "PassVertex";
		static constexpr const char* k_sDefaultFragmentEntryPoint = "PassFragment";

		// Reads a .vsf file from disk and parses it. Returns FailCannotOpenFile
		// when it cannot be read.
		HlslccResult LoadFromFile(const std::string& sFilePath, std::string& outErrorText);

		// Parses source text that is already in memory. Plain HLSL without any
		// block is accepted and becomes a single pass.
		HlslccResult LoadFromSource(const std::string& sSourceText, const std::string& sDisplayName);

		const std::string& GetSourceText() const { return m_sSourceText; }
		const std::string& GetDisplayName() const { return m_sDisplayName; }
		const std::string& GetFilePath() const { return m_sFilePath; }

		// "Shader \"Name\"" of the file; falls back to the display name.
		const std::string& GetShaderName() const { return m_sShaderName; }
		void SetShaderName(const std::string& sShaderName) { m_sShaderName = sShaderName; }

		// Render queue the shader belongs to (default: Geometry).
		uint32_t GetRenderQueue() const { return m_uRenderQueue; }
		void SetRenderQueue(uint32_t uRenderQueue) { m_uRenderQueue = uRenderQueue; }

		const std::vector<ShaderProperty>& GetProperties() const { return m_Properties; }
		const std::vector<ShaderKeywordGroup>& GetKeywordGroups() const { return m_KeywordGroups; }

		uint32_t GetPassCount() const { return static_cast<uint32_t>(m_Passes.size()); }
		const ShaderPass& GetPass(uint32_t uPassIndex) const { return m_Passes[uPassIndex]; }

		// Directory the file lives in: the base for its #include directives.
		std::string GetSourceDirectory() const;

		// True when the pass declares a function with this name.
		static bool SourceDeclaresFunction(const std::string& sSourceText, const std::string& sFunctionName);

		// Parses a "#pragma <name>" line out of a pass body. Returns false when the
		// line is not that pragma.
		static bool ParsePragmaLine(const std::string& sLine, const std::string& sPragmaName, std::vector<std::string>& outArguments);

		// Adds or replaces a keyword group declared by the command line.
		void AddOrReplaceKeywordGroup(const ShaderKeywordGroup& keywordGroup);

		// Overrides the entry point of every pass (used by the command line).
		void SetEntryPoint(ShaderStage eStage, const std::string& sEntryPointName);

		// True when any pass declares the entry point of the given stage.
		bool HasEntryPoint(ShaderStage eStage) const;

	private:
		// Splits the source into the blocks and fills every table above.
		void ParseBlocks();

		// Parses one "Properties { ... }" body.
		void ParsePropertiesBlock(const std::string& sBody);

		// Parses one "Shader { ... }" body.
		void ParseShaderBlock(const std::string& sBody);

		// Parses one "Pass { ... }" body.
		void ParsePassBlock(const std::string& sPassName, const std::string& sBody);

		std::string m_sSourceText;
		std::string m_sDisplayName;
		std::string m_sFilePath;

		std::string m_sShaderName;
		uint32_t m_uRenderQueue = 2000;   // Geometry.
		std::vector<ShaderProperty> m_Properties;
		std::vector<ShaderKeywordGroup> m_KeywordGroups;
		std::vector<ShaderPass> m_Passes;
	};
}
