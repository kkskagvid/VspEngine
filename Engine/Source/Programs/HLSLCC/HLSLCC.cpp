// -------------------------------------------------------------------------
// HLSLCC - the engine's HLSL cross compiler.
//
// Compiles ONE HLSL file that holds the entry point of every stage (PassVertex
// and PassFragment by default) into one SPIR-V module per stage plus a
// reflection document. The compiler itself lives in the HLSLCC library, which
// the engine also links, so offline and runtime compilation produce identical
// results.
//
// See CommandLine.h for the accepted arguments.
// -------------------------------------------------------------------------

#include <cstdio>

#include "CommandLine.h"

int main(int argc, char* argv[])
{
	const Hlslcc::CommandLineOptions options = Hlslcc::CommandLine::Parse(argc, argv);

	if (options.bShowHelp)
	{
		Hlslcc::CommandLine::PrintUsage();
		return 0;
	}

	return Hlslcc::CommandLine::Run(options);
}
