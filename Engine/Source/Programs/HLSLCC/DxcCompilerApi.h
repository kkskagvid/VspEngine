#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Result.h"
#include "ShaderReflection.h"

namespace Hlslcc
{
	// -------------------------------------------------------------------------
	// DxcCompilerApi
	// -------------------------------------------------------------------------
	// Thin wrapper around the DirectX Shader Compiler (dxcompiler.dll) that
	// compiles ONE entry point of an HLSL file to SPIR-V.
	//
	// The library is loaded dynamically, so HLSLCC runs on machines whose DXC
	// lives anywhere the search below finds it (next to the executable, the
	// repository's Thirdparty/dxc, or the Vulkan SDK) and needs no import
	// library at link time. Nothing throws: every failure is a HlslccResult and
	// a message in outErrorText.
	// -------------------------------------------------------------------------

	// One compile request: the source plus everything the compiler is told.
	struct CompileRequest
	{
		std::string SourceText;
		std::string SourceName;                        // Used in compiler diagnostics.
		std::string EntryPoint;                        // Function compiled out of the file.
		ShaderStage eStage = ShaderStage::Vertex;
		std::string TargetEnvironment = "vulkan1.3";   // -fspv-target-env
		std::vector<std::string> IncludeDirectories;
		std::vector<std::string> Defines;              // "NAME" or "NAME=VALUE".
		bool bDebugInfo = false;
	};

	// The SPIR-V module produced for one entry point.
	struct CompileOutput
	{
		std::vector<uint32_t> SpirvWords;   // Complete SPIR-V module.
		std::string Diagnostics;            // Warnings/errors the compiler produced.
	};

	class DxcCompilerApi
	{
	public:
		static DxcCompilerApi& Get();

		// Locates and loads dxcompiler.dll and creates the compiler objects.
		// Safe to call repeatedly: after the first success it is a no-op.
		HlslccResult Initialize(std::string& outErrorText);

		bool IsAvailable();

		// Directory the loaded dxcompiler.dll came from; empty while unavailable.
		const std::string& GetLoadedLibraryDirectory() const { return m_sLoadedLibraryDirectory; }

		// Compiles one entry point to SPIR-V.
		HlslccResult CompileToSpirv(
			const CompileRequest& request,
			CompileOutput& outOutput,
			std::string& outErrorText);

		// Turns a SPIR-V stage into a readable module header summary (magic,
		// version, bound) - used by the tool's verbose output and by tests.
		static bool DescribeSpirvModule(const std::vector<uint32_t>& spirvWords, std::string& outDescription);

		// The SPIR-V magic number every module starts with.
		static constexpr uint32_t k_uSpirvMagicNumber = 0x07230203u;

		// Releases the compiler objects and unloads the library. Call it while
		// the process is still healthy; the singleton itself is never destroyed
		// (see Get()).
		void Shutdown();

	private:
		DxcCompilerApi() = default;
		~DxcCompilerApi();

		DxcCompilerApi(const DxcCompilerApi&) = delete;
		DxcCompilerApi& operator=(const DxcCompilerApi&) = delete;

		// Builds the candidate dxcompiler.dll paths in search order.
		static void CollectLibraryCandidates(std::vector<std::string>& outCandidatePaths);

		void* m_pCompilerLibrary = nullptr;
		void* m_pUtils = nullptr;
		void* m_pCompiler = nullptr;
		std::string m_sLoadedLibraryDirectory;
	};
}
