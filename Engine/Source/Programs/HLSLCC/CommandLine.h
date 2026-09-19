#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Compiler.h"
#include "Result.h"

namespace Hlslcc
{
	// -------------------------------------------------------------------------
	// CommandLineOptions
	// -------------------------------------------------------------------------
	// Everything HLSLCC.exe accepts on its command line.
	//
	//   HLSLCC <shader.vsf> [options]
	//
	//     --output-directory <dir>   where the .spv/.json files are written
	//                                (default: next to the shader file)
	//     --used-variant <state>     a keyword state the build uses; repeatable.
	//                                Strippable keyword groups (variant /
	//                                variant_local) keep only the states listed
	//                                here, and their default state when none is.
	//     --vertex-entry <name>      entry point for passes without a
	//                                "#pragma vertex"   (default PassVertex)
	//     --fragment-entry <name>    entry point for passes without a
	//                                "#pragma fragment" (default PassFragment)
	//     --target-env <env>         SPIR-V target environment (default vulkan1.3)
	//     -I <dir>                   extra include directory
	//     -D <name>[=value]          preprocessor definition
	//     --no-vulkan-namespace      do not inject the Vulkan HLSL namespace
	//     --no-attribute-shorthands  do not inject the attribute shorthands
	//     --builtin-directory <dir>  where the HLSL builtin library lives
	//                                (default: <exe dir>\Shaders\Builtin)
	//     --reflection-only          only write the reflection/manifest documents
	//     --debug-info               compile unoptimised with debug information
	//     --quiet                    only report failures
	//     --help                     print the usage text
	// -------------------------------------------------------------------------
	struct CommandLineOptions
	{
		std::string ShaderFilePath;
		std::string OutputDirectory;

		// Directory holding the HLSL builtin library a shader reaches with
		// "#include <Vsp/...>". Empty means "find it next to the compiler".
		std::string BuiltinDirectory;

		CompileOptions CompileOptions;

		bool bReflectionOnly = false;
		bool bQuiet = false;
		bool bShowHelp = false;

		// Set when the command line itself is unusable.
		std::string ParseErrorText;
	};

	class CommandLine
	{
	public:
		// Parses the arguments (argv[0] is the executable name).
		static CommandLineOptions Parse(int32_t nArgumentCount, char** pArguments);

		// Runs one compilation from parsed options. Returns the process exit code.
		static int32_t Run(const CommandLineOptions& options);

		// Prints the usage text.
		static void PrintUsage();
	};
}
