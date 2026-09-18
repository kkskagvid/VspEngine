#pragma once

#include <string>
#include <vector>

namespace Hlslcc
{
	// -------------------------------------------------------------------------
	// FilePathUtility
	// -------------------------------------------------------------------------
	// The few file-system helpers HLSLCC needs. Every relative candidate path is
	// resolved against the DIRECTORY OF THE EXECUTABLE rather than the current
	// working directory, so the compiler finds the DXC installation next to it no
	// matter where a build system invokes it from.
	// -------------------------------------------------------------------------
	class FilePathUtility
	{
	public:
		// Directory the running executable lives in (no trailing separator);
		// empty when it cannot be determined.
		static std::string GetExecutableDirectory();

		// Joins two path fragments with the platform separator.
		static std::string Combine(const std::string& sLeftPath, const std::string& sRightPath);

		// Resolves a path against the current working directory. Absolute paths
		// come back with their ".." segments folded away; an empty or unresolvable
		// path comes back unchanged.
		static std::string GetAbsolutePath(const std::string& sFilePath);

		// Last path component ("a/b/c.txt" -> "c.txt").
		static std::string GetFileName(const std::string& sFilePath);

		// True when the path addresses an existing file.
		static bool DoesFileExist(const std::string& sFilePath);

		// True when the path addresses an existing directory.
		static bool DoesDirectoryExist(const std::string& sDirectoryPath);

		// Creates the directory (and its parents) when it does not exist yet.
		// Returns true when the directory exists afterwards.
		static bool EnsureDirectoryExists(const std::string& sDirectoryPath);

		// Value of an environment variable; empty when it is unset or empty.
		static std::string GetEnvironmentValue(const char* pVariableName);

		// File name without its extension ("a/b/c.txt" -> "c").
		static std::string GetFileBaseName(const std::string& sFilePath);

		// Directory part of a path; "." when it has none.
		static std::string GetDirectory(const std::string& sFilePath);
	};
}
