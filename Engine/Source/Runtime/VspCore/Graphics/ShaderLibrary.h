#pragma once

#include "Classes/Object.h"
#include "Classes/Shader.h"
#include "Core/Core.h"
#include "Core/String/VspString.h"
#include "Core/Templates/ArrayList.h"

namespace Vsp
{
	// -------------------------------------------------------------------------
	// ShaderLibrary
	// -------------------------------------------------------------------------
	// Loads the shaders HLSLCC compiled into the engine. A compiled shader is a
	// pair of files next to the executable:
	//
	//   <Name>.shader.json   the manifest: the shader's name, its render queue,
	//                        the properties, the keyword groups and, for every
	//                        variant of every pass, the module files to read
	//   <Name>.*.spv         the SPIR-V modules the manifest names
	//
	// Loading turns them into a native Shader object (registered in the Scene)
	// and caches it by name, so a shader is read from disk once.
	// Every function reports failure through return values and logs the reason;
	// nothing throws.
	// -------------------------------------------------------------------------
#pragma warning(push)
#pragma warning(disable : 4251)   // ArrayList member: header-only template.
	class RUNTIME_API ShaderLibrary
	{
	public:
		static ShaderLibrary& Get();

		// Directory the compiled shaders are read from. Defaults to the
		// "Shaders" directory next to the executable.
		void SetShaderDirectory(const VspString& sShaderDirectory) { m_sShaderDirectory = sShaderDirectory; }
		const VspString& GetShaderDirectory() const { return m_sShaderDirectory; }

		// Builds the default shader directory from the executable location.
		void UseDefaultShaderDirectory();

		// Reads <sShaderName>.shader.json and the modules it names. Returns the
		// native shader handle (0 on failure).
		NativeObjectHandle LoadShader(const VspString& sShaderName, VspString& outErrorText);

		// Handle of an already loaded shader; loads it on first use.
		NativeObjectHandle FindOrLoadShader(const VspString& sShaderName, VspString& outErrorText);

		// Creates a material for an already loaded shader.
		NativeObjectHandle CreateMaterial(NativeObjectHandle uShaderHandle, VspString& outErrorText);

		// Releases the cache (the Scene still owns the shader objects).
		void ClearCache() { m_Entries.Clear(); }

		uint32 GetLoadedShaderCount() const { return static_cast<uint32>(m_Entries.GetSize()); }

	private:
		ShaderLibrary() = default;

		struct Entry
		{
			VspString sName;
			NativeObjectHandle uShaderHandle = k_nInvalidObjectHandle;
		};

		const Entry* FindEntry(const VspString& sShaderName) const;

		// Reads one module file into the stage it belongs to.
		bool LoadStageModule(
			const VspString& sModulePath,
			Shader::StageModule& outStage,
			VspString& outErrorText) const;

		ArrayList<Entry> m_Entries;
		VspString m_sShaderDirectory;
	};
#pragma warning(pop)
}
