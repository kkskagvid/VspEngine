#pragma once

#include <string>
#include <vector>

#include "Compiler.h"
#include "Result.h"

namespace Hlslcc
{
	// -------------------------------------------------------------------------
	// VsfoWriter
	// -------------------------------------------------------------------------
	// Writes the shader container (".vsfo"): the file the engine loads, holding
	// every SPIR-V module of a compilation together with the reflection of each
	// of them.
	//
	// The container is an index table followed by a data segment (see
	// Shared/VsfoFormat.h for the layout): the index table says WHICH STAGE each
	// module belongs to, HOW BIG it is and WHAT it reflects, and the data
	// segment holds the modules and their reflection records. A reader therefore
	// never has to parse anything to find a stage.
	//
	// Nothing throws; every failure is a HlslccResult and a message.
	// -------------------------------------------------------------------------
	class VsfoWriter
	{
	public:
		// Path of the container: "<directory>/<baseName>.vsfo".
		static std::string BuildContainerPath(const std::string& sOutputDirectory, const std::string& sBaseName);

		// Builds the whole container in memory. Fails when a name does not fit
		// the format or a stage produced no module.
		static HlslccResult BuildContainer(
			const CompiledShader& shader,
			const std::string& sBaseName,
			std::vector<uint8_t>& outBytes,
			std::string& outErrorText);

		// Builds the container and writes it to sFilePath.
		static HlslccResult WriteContainer(
			const std::string& sFilePath,
			const CompiledShader& shader,
			const std::string& sBaseName,
			std::string& outErrorText);

		// One line per module of a written container, for the console summary.
		static std::string BuildContainerSummary(const CompiledShader& shader, const std::string& sBaseName);
	};
}
