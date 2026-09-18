#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Hlslcc
{
	// -------------------------------------------------------------------------
	// Shader definition
	// -------------------------------------------------------------------------
	// The parts of a .vsf shader file that are NOT HLSL: the properties the editor
	// shows, the keyword groups that drive shader variants, and the render queue.
	// The parser fills them in and the compiler turns the keyword groups into the
	// variant list it compiles.
	// -------------------------------------------------------------------------

	static constexpr uint32_t k_nMaxShaderNameLength = 128;
	static constexpr uint32_t k_nMaxShaderKeywordLength = 64;
	static constexpr uint32_t k_nMaxShaderKeywordStateCount = 16;
	static constexpr uint32_t k_nMaxShaderKeywordGroupCount = 8;
	static constexpr uint32_t k_nMaxShaderVariantCount = 64;
	static constexpr uint32_t k_nMaxShaderPassCount = 8;
	static constexpr uint32_t k_nMaxShaderPropertyCount = 32;

	// The "no keyword is defined" state of a group is spelled '_' both in the
	// shader file and on the command line.
	static constexpr const char* k_sNoKeywordStateName = "_";

	// -------------------------------------------------------------------------
	// Keywords and variants
	// -------------------------------------------------------------------------
	// Where a keyword group came from, which decides whether the build keeps every
	// variant or only the ones it actually uses:
	//
	//   #pragma variant             / variant_local             -> STRIPPABLE
	//   #pragma multi_variant       / multi_variant_local       -> always kept
	//
	// A strippable group keeps only the states the build reported as used (see
	// --used-variant); when nothing was reported for it, only its first state is
	// kept. An "always kept" group keeps every state.
	//
	// The *_local forms declare LOCAL keywords - keywords a single material turns
	// on and off - while the plain forms declare GLOBAL keywords shared by the
	// whole build.
	enum class ShaderKeywordKind
	{
		Variant = 0,
		VariantLocal = 1,
		MultiVariant = 2,
		MultiVariantLocal = 3,
	};

	// Text the shader file and the reports use ("variant", "variant_local", ...).
	const char* ToKeywordKindName(ShaderKeywordKind eKind);

	// Parses a keyword kind name; returns false when it is not one.
	bool ParseKeywordKind(const std::string& sName, ShaderKeywordKind& outKind);

	// True for the kinds whose unused variants are stripped from the build.
	bool IsKeywordKindStrippable(ShaderKeywordKind eKind);

	// True for the *_local kinds (keywords a material turns on itself).
	bool IsKeywordKindLocal(ShaderKeywordKind eKind);

	// One keyword group of a shader.
	struct ShaderKeywordGroup
	{
		// Name the group is reported under: the keyword names joined by '|'.
		char Name[k_nMaxShaderKeywordLength] = {};

		ShaderKeywordKind eKind = ShaderKeywordKind::Variant;

		// One entry per state, in declaration order. State 0 is the default.
		// A state named "_" means "no keyword is defined".
		uint32_t uKeywordStateCount = 0;
		char KeywordStates[k_nMaxShaderKeywordStateCount][k_nMaxShaderKeywordLength] = {};

		// True when this group's unused variants are stripped.
		bool IsStrippable() const { return IsKeywordKindStrippable(eKind); }
	};

	// Builds the group name ("A|B") from the declared keyword names.
	void BuildKeywordGroupName(
		const std::vector<std::string>& sKeywordNames,
		char (&outName)[k_nMaxShaderKeywordLength]);

	// Turns a list of declared keyword names into a group. A single name that is
	// not "_" becomes a two-state on/off group; every other list is used as it
	// stands (with "_" meaning "no keyword").
	ShaderKeywordGroup BuildKeywordGroup(
		const std::vector<std::string>& sKeywordNames,
		ShaderKeywordKind eKind);

	// True when the state name means "no keyword is defined".
	bool IsNoKeywordState(const char* pKeywordStateName);

	// -------------------------------------------------------------------------
	// Properties
	// -------------------------------------------------------------------------
	enum class ShaderPropertyType
	{
		Float = 0,
		Vector = 1,
		Color = 2,
		Texture = 3,
	};

	const char* ToPropertyTypeName(ShaderPropertyType eType);
	bool ParsePropertyType(const std::string& sName, ShaderPropertyType& outType);

	struct ShaderProperty
	{
		char Name[k_nMaxShaderKeywordLength] = {};
		char DisplayName[k_nMaxShaderNameLength] = {};

		ShaderPropertyType eType = ShaderPropertyType::Float;

		// Default values; a texture property leaves them at zero.
		float fDefaultValues[4] = {};
	};

	// -------------------------------------------------------------------------
	// Variants
	// -------------------------------------------------------------------------
	// One point in the keyword space: the state each group is in.
	struct ShaderVariantKey
	{
		// State index per group, in group order.
		uint32_t uKeywordStateIndices[k_nMaxShaderKeywordGroupCount] = {};

		// Enabled keyword names joined by '+'; empty when no keyword is defined.
		std::string KeyText;

		// "NAME" lines the compiler is given, one preprocessor definition each.
		std::vector<std::string> Defines;

		// True when the key defines no keyword at all (the default variant).
		bool IsDefault() const { return KeyText.empty(); }
	};

	// Builds the variant keys a shader is compiled for, applying the strip rules:
	// a strippable group keeps the states sUsedKeywordStateNames mentions, or only
	// its first state when the build reported none; every other group keeps all of
	// its states.
	//
	// Returns false (with outErrorText) when the keyword space is larger than
	// k_nMaxShaderVariantCount.
	bool BuildShaderVariantKeys(
		const std::vector<ShaderKeywordGroup>& keywordGroups,
		const std::vector<std::string>& sUsedKeywordStateNames,
		std::vector<ShaderVariantKey>& outVariantKeys,
		std::string& outErrorText);

	// Human-readable report of what the strip rules kept and dropped.
	std::string BuildVariantStripReport(
		const std::vector<ShaderKeywordGroup>& keywordGroups,
		const std::vector<ShaderVariantKey>& keptVariantKeys);
}
