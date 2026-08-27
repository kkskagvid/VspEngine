#pragma once

#include "Core/Core.h"
#include "Core/String/VspString.h"
#include "Core/Templates/ArrayList.h"
#include "Scripting/BridgeFunctions.h"

namespace Vsp
{
	// Non-negative integer handle addressing one managed script instance.
	// 0 means invalid (managed ids start at 1).
	using ScriptInstanceId = uint32_t;

	// -------------------------------------------------------------------------
	// ScriptEngine
	// -------------------------------------------------------------------------
	// Embeds the .NET CoreCLR runtime into the C++ host through the CoreCLR
	// Hosting API (nethost + hostfxr). The host:
	//   1. loads nethost.dll and asks it for the hostfxr.dll path,
	//   2. initializes hostfxr against a runtimeconfig.json,
	//   3. fetches raw function pointers into the managed NativeBridge,
	//   4. creates script instances and drives their lifecycle by InstanceID.
	// The reverse direction (C# -> C++) goes through the exports in
	// NativeExports.cpp, which managed code reaches with DllImport.
	// All functions report failure through return values / outErrorText —
	// the engine never uses C++ exceptions.
	// -------------------------------------------------------------------------
#pragma warning(push)
#pragma warning(disable : 4251)   // ArrayList member: header-only template.
	class RUNTIME_API ScriptEngine
	{
	public:
		static ScriptEngine& Get();

		// Loads nethost/hostfxr, initializes the runtime and resolves every
		// managed bridge entry point. Call once before any script work.
		bool Initialize(
			const VspString& sAssemblyPath,
			const VspString& sRuntimeConfigPath,
			const VspString& sDotNetRootPath,
			VspString& outErrorText);

		// Destroys every script instance and closes the hostfxr context.
		void Shutdown();

		bool IsInitialized() const { return m_hHostFxrLibrary != nullptr; }

		// Asks the managed bridge to create one instance of every concrete
		// ScriptBehaviour type in the game assembly. Returns false on failure.
		bool CreateAllScriptInstances(VspString& outErrorText);

		// Creates one instance of the given assembly-qualified type name.
		ScriptInstanceId CreateScriptInstance(const VspString& sTypeName);

		void CallScriptInit(ScriptInstanceId uInstanceId);
		void CallScriptStart(ScriptInstanceId uInstanceId);
		void CallScriptUpdate(ScriptInstanceId uInstanceId);
		void DestroyScriptInstance(ScriptInstanceId uInstanceId);

		// Invokes OnUpdate for every registered instance.
		void UpdateAllScripts();

		uint32_t GetScriptInstanceCount() const { return static_cast<uint32_t>(m_ScriptInstanceIds.GetSize()); }
		ScriptInstanceId GetPrimaryScriptInstanceId() const
		{
			return m_ScriptInstanceIds.IsEmpty() ? 0 : m_ScriptInstanceIds[0];
		}

	private:
		ScriptEngine() = default;

		bool LoadNetHostLibrary(const VspString& sDotNetRootPath, VspString& outErrorText);
		bool LoadHostFxrLibrary(VspString& outErrorText);
		bool InitializeHostFxrRuntime(const VspString& sRuntimeConfigPath, VspString& outErrorText);
		bool LoadManagedEntryPoints(const VspString& sAssemblyPath, VspString& outErrorText);

		static bool FetchManagedEntryPoint(
			load_assembly_and_get_function_pointer_fn pLoadAssemblyFunction,
			const wchar_t* pAssemblyPathUtf16,
			const wchar_t* pTypeNameUtf16,
			const wchar_t* pMethodNameUtf16,
			void** ppOutFunction,
			VspString& outErrorText);

		HMODULE m_hNetHostLibrary = nullptr;
		HMODULE m_hHostFxrLibrary = nullptr;
		hostfxr_handle m_RuntimeContext = nullptr;
		load_assembly_and_get_function_pointer_fn m_LoadAssemblyFunction = nullptr;
		BridgeFunctions m_BridgeFunctions;
		ArrayList<ScriptInstanceId> m_ScriptInstanceIds;
	};
#pragma warning(pop)
}
