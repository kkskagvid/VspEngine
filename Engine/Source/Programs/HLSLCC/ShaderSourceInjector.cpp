#include "ShaderSourceInjector.h"

#include <cstring>

#include "FilePathUtility.h"

namespace Hlslcc
{
	namespace
	{
		// Prologue injected in front of every shader. It is guarded, so a shader
		// that already carries it (or that opts out) is never affected twice.
		const char* const k_sVulkanNamespacePrologue =
			"// ---------------------------------------------------------------------------\n"
			"// Injected by HLSLCC: the Vulkan HLSL namespace.\n"
			"// ---------------------------------------------------------------------------\n"
			"#ifndef VSP_VULKAN_NAMESPACE_INJECTED\n"
			"#define VSP_VULKAN_NAMESPACE_INJECTED 1\n"
			"#include <vk/spirv.h>\n"
			"#endif\n";

		const char* const k_sEngineAttributePrologue =
			"// ---------------------------------------------------------------------------\n"
			"// Injected by HLSLCC: the engine's Vulkan attribute shorthands.\n"
			"// ---------------------------------------------------------------------------\n"
			"#ifndef VSP_VULKAN_ATTRIBUTES_INJECTED\n"
			"#define VSP_VULKAN_ATTRIBUTES_INJECTED 1\n"
			"#define VSP_VK_BINDING(Binding, Set)         [[vk::binding(Binding, Set)]]\n"
			"#define VSP_VK_LOCATION(Location)            [[vk::location(Location)]]\n"
			"#define VSP_VK_PUSH_CONSTANT                 [[vk::push_constant]]\n"
			"#define VSP_VK_INDEX(Index)                  [[vk::index(Index)]]\n"
			"#define VSP_VK_OFFSET(ByteOffset)            [[vk::offset(ByteOffset)]]\n"
			"#define VSP_VK_BUILTIN(BuiltIn)              [[vk::builtin(BuiltIn)]]\n"
			"#define VSP_VK_INPUT_ATTACHMENT_INDEX(Index) [[vk::input_attachment_index(Index)]]\n"
			"#endif\n";

		// Counts the lines a text block consists of (used to map diagnostics).
		uint32_t CountLines(const std::string& sText)
		{
			uint32_t uLineCount = 0;
			for (char cCharacter : sText)
			{
				if (cCharacter == '\n')
				{
					++uLineCount;
				}
			}
			return uLineCount;
		}
	}

	bool ShaderSourceInjector::IsInjectionDisabled(const std::string& sSourceText)
	{
		return sSourceText.find(k_sVulkanNamespaceOptOutMacro) != std::string::npos ||
			sSourceText.find(k_sVulkanNamespaceGuardMacro) != std::string::npos;
	}

	bool ShaderSourceInjector::SourceTextSpecifiesBindings(const std::string& sSourceText)
	{
		// The spellings a shader can name a binding with: the vk:: attribute, the
		// DXC register annotation and the shorthand this injector defines. A file
		// that uses the shorthand is caught by the shorthand, and a file that
		// defines it is caught by the attribute inside the definition.
		return sSourceText.find("vk::binding") != std::string::npos ||
			sSourceText.find("register(") != std::string::npos ||
			sSourceText.find("VSP_VK_BINDING") != std::string::npos;
	}

	size_t ShaderSourceInjector::FindPrologueInsertOffset(const std::string& sSourceText)
	{
		// Walk the leading run of blank lines, line comments, block comments and
		// preprocessor directives; the prologue goes right after it. That keeps
		// the shader's own leading #defines (including the opt-out macro) ahead
		// of the injected block.
		size_t nOffset = 0;
		bool bInBlockComment = false;

		while (nOffset < sSourceText.size())
		{
			const size_t nLineEndOffset = sSourceText.find('\n', nOffset);
			const size_t nNextOffset = (nLineEndOffset == std::string::npos) ? sSourceText.size() : nLineEndOffset + 1;
			const std::string sLine = sSourceText.substr(nOffset, nNextOffset - nOffset);

			size_t nCursor = 0;
			while (nCursor < sLine.size() && (sLine[nCursor] == ' ' || sLine[nCursor] == '\t' || sLine[nCursor] == '\r'))
			{
				++nCursor;
			}

			if (bInBlockComment)
			{
				const size_t nCommentEnd = sLine.find("*/", nCursor);
				if (nCommentEnd == std::string::npos)
				{
					nOffset = nNextOffset;
					continue;
				}
				bInBlockComment = false;
				nCursor = nCommentEnd + 2;
				while (nCursor < sLine.size() && (sLine[nCursor] == ' ' || sLine[nCursor] == '\t' || sLine[nCursor] == '\r'))
				{
					++nCursor;
				}
			}

			if (nCursor >= sLine.size() || sLine[nCursor] == '\n' || sLine[nCursor] == '\r')
			{
				nOffset = nNextOffset;   // Blank line.
				continue;
			}

			if (sLine.compare(nCursor, 2, "//") == 0)
			{
				nOffset = nNextOffset;   // Line comment.
				continue;
			}

			if (sLine.compare(nCursor, 2, "/*") == 0)
			{
				bInBlockComment = true;
				nOffset = nNextOffset;
				continue;
			}

			if (sLine[nCursor] == '#')
			{
				// The prologue has to land in front of the shader's first
				// #include, otherwise the included file would not see the macros
				// and the Vulkan namespace. Plain #defines stay ahead of it, so a
				// shader can still opt out with a leading define.
				if (sLine.compare(nCursor, 8, "#include") == 0)
				{
					break;
				}

				nOffset = nNextOffset;   // Preprocessor directive.
				continue;
			}

			break;   // First real declaration: the prologue belongs here.
		}

		return nOffset;
	}

	bool ShaderSourceInjector::FindVulkanNamespaceIncludeDirectory(std::string& outIncludeDirectory)
	{
		outIncludeDirectory.clear();

		// Every candidate is an "include root": the directory that carries
		// "vk/spirv.h" is "<root>/hlsl", which is what the compiler is given.
		std::vector<std::string> sIncludeRoots;

		const std::string sDxcRoot = FilePathUtility::GetEnvironmentValue("VSP_DXC_ROOT");
		if (!sDxcRoot.empty())
		{
			sIncludeRoots.push_back(FilePathUtility::Combine(sDxcRoot, "inc"));
		}
		const std::string sDxcRootAlternative = FilePathUtility::GetEnvironmentValue("DXC_ROOT");
		if (!sDxcRootAlternative.empty())
		{
			sIncludeRoots.push_back(FilePathUtility::Combine(sDxcRootAlternative, "inc"));
		}
		const std::string sVulkanSdkRoot = FilePathUtility::GetEnvironmentValue("VULKAN_SDK");
		if (!sVulkanSdkRoot.empty())
		{
			sIncludeRoots.push_back(FilePathUtility::Combine(sVulkanSdkRoot, "Include\\dxc"));
			sIncludeRoots.push_back(FilePathUtility::Combine(sVulkanSdkRoot, "Include"));
		}

		// The copy vendored in the repository, reached from the executable of
		// whichever module is asking (HLSLCC.exe or the engine).
		const std::string sExecutableDirectory = FilePathUtility::GetExecutableDirectory();
		if (!sExecutableDirectory.empty())
		{
			sIncludeRoots.push_back(FilePathUtility::Combine(sExecutableDirectory, "..\\..\\..\\Source\\Thirdparty\\dxc\\inc"));
			sIncludeRoots.push_back(FilePathUtility::Combine(sExecutableDirectory, "..\\Thirdparty\\dxc\\inc"));
		}
		sIncludeRoots.push_back("Engine\\Source\\Thirdparty\\dxc\\inc");

		for (const std::string& sIncludeRoot : sIncludeRoots)
		{
			const std::string sVulkanNamespaceDirectory = FilePathUtility::Combine(sIncludeRoot, "hlsl");
			if (FilePathUtility::DoesFileExist(
				FilePathUtility::Combine(sVulkanNamespaceDirectory, k_sVulkanNamespaceHeader)))
			{
				outIncludeDirectory = sVulkanNamespaceDirectory;
				return true;
			}
		}

		return false;
	}

	HlslccResult ShaderSourceInjector::Inject(
		const std::string& sSourceText,
		const InjectionOptions& options,
		InjectionResult& outResult,
		std::string& outErrorText)
	{
		outErrorText.clear();
		outResult = InjectionResult();

		if (!options.bInjectVulkanNamespace && !options.bInjectEngineAttributeShorthands)
		{
			outResult.SourceText = sSourceText;
			return HlslccResult::Success;
		}

		if (IsInjectionDisabled(sSourceText))
		{
			// The shader carries its own copy (or asked to be left alone).
			outResult.SourceText = sSourceText;
			return HlslccResult::Success;
		}

		std::string sPrologue;
		if (options.bInjectVulkanNamespace)
		{
			std::string sIncludeDirectory;
			if (FindVulkanNamespaceIncludeDirectory(sIncludeDirectory))
			{
				sPrologue += k_sVulkanNamespacePrologue;
				outResult.bVulkanNamespaceHeaderIncluded = true;
				outResult.VulkanNamespaceIncludeDirectory = sIncludeDirectory;
			}
		}

		if (options.bInjectEngineAttributeShorthands)
		{
			sPrologue += k_sEngineAttributePrologue;
			outResult.bEngineAttributeShorthandsIncluded = true;
		}

		if (sPrologue.empty())
		{
			outResult.SourceText = sSourceText;
			return HlslccResult::Success;
		}

		const size_t nInsertOffset = FindPrologueInsertOffset(sSourceText);
		outResult.SourceText.reserve(sSourceText.size() + sPrologue.size());
		outResult.SourceText.append(sSourceText, 0, nInsertOffset);
		outResult.SourceText += sPrologue;
		outResult.SourceText.append(sSourceText, nInsertOffset, std::string::npos);
		outResult.uInjectedLineCount = CountLines(sPrologue);
		return HlslccResult::Success;
	}
}
