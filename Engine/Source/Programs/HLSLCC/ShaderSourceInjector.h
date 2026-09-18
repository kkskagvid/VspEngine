#pragma once

#include <string>

#include "Result.h"

namespace Hlslcc
{
	// -------------------------------------------------------------------------
	// ShaderSourceInjector
	// -------------------------------------------------------------------------
	// Prepares shader source for the HLSL compiler by injecting the prologue the
	// engine relies on, so a shader file only contains the shader itself:
	//
	//   1. the VULKAN HLSL NAMESPACE - "#include <vk/spirv.h>" (plus the include
	//      directory that holds it) makes DXC's vk:: namespace available without
	//      the shader knowing where the compiler keeps its headers;
	//   2. the engine attribute shorthands (VSP_VK_BINDING, VSP_VK_LOCATION, ...)
	//      that wrap the [[vk::...]] spelling.
	//
	// The prologue is inserted after the file's leading preprocessor block, so a
	// shader can still opt out with a leading
	//     #define VSP_NO_VULKAN_NAMESPACE
	// or by declaring VSP_VULKAN_NAMESPACE_INJECTED itself. Injection is
	// idempotent: a file that already carries the include is left alone.
	// -------------------------------------------------------------------------

	// Guard macro the injected prologue defines and tests.
	static constexpr const char* k_sVulkanNamespaceGuardMacro = "VSP_VULKAN_NAMESPACE_INJECTED";

	// Macro a shader defines to switch the injection off.
	static constexpr const char* k_sVulkanNamespaceOptOutMacro = "VSP_NO_VULKAN_NAMESPACE";

	// Header providing DXC's vk:: namespace, relative to the HLSL include root.
	static constexpr const char* k_sVulkanNamespaceHeader = "vk/spirv.h";

	struct InjectionOptions
	{
		// Inject the Vulkan HLSL namespace (available even when the header is
		// missing: only the #include is skipped then).
		bool bInjectVulkanNamespace = true;

		// Inject the engine attribute shorthands.
		bool bInjectEngineAttributeShorthands = true;
	};

	struct InjectionResult
	{
		// Source text handed to the compiler.
		std::string SourceText;

		// Number of lines prepended (the compiler reports line numbers relative
		// to this text, so diagnostics can be mapped back).
		uint32_t uInjectedLineCount = 0;

		bool bVulkanNamespaceHeaderIncluded = false;
		bool bEngineAttributeShorthandsIncluded = false;

		// Include directory that must be handed to the compiler so the injected
		// #include resolves; empty when no include was injected.
		std::string VulkanNamespaceIncludeDirectory;
	};

	class ShaderSourceInjector
	{
	public:
		// Injects the requested prologue into sSourceText.
		static HlslccResult Inject(
			const std::string& sSourceText,
			const InjectionOptions& options,
			InjectionResult& outResult,
			std::string& outErrorText);

		// Looks for "<dxcRoot>/inc/hlsl/vk/spirv.h". Returns false when the
		// compiler installation has no Vulkan namespace header; outIncludeDirectory
		// then stays empty.
		static bool FindVulkanNamespaceIncludeDirectory(std::string& outIncludeDirectory);

		// True when the source text already asks for the injection to be skipped.
		static bool IsInjectionDisabled(const std::string& sSourceText);

	private:
		// Byte offset the prologue is inserted at: past the leading run of blank
		// lines, comments, preprocessor directives and whitespace.
		static size_t FindPrologueInsertOffset(const std::string& sSourceText);
	};
}
