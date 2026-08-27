#include "RuntimePCH.h"

#include <cstdio>

#include <Windows.h>

#include "Core/Logging/Log.h"
#include "Scripting/ScriptEngine.h"

namespace Vsp
{
	static constexpr const char* kLogTag = "ScriptEngine";

	// The managed bridge type (assembly-qualified) hosting the entry points.
	static constexpr const wchar_t* k_sManagedBridgeTypeName = L"VspEngine.NativeBridge, VspPlayer";

	// Formats a 32-bit value as an 8-digit hexadecimal string for error text.
	static VspString FormatHex(int32_t nValue)
	{
		char sBuffer[32];
		snprintf(sBuffer, sizeof(sBuffer), "%08X", static_cast<uint32_t>(nValue));
		return VspString(sBuffer);
	}

	// -------------------------------------------------------------------------
	// Lifecycle
	// -------------------------------------------------------------------------

	// nethost.h declares but does not typedef the function pointer.
	using GetHostFxrPathFn = int (NETHOST_CALLTYPE*)(
		char_t* pBuffer,
		size_t* pBufferSize,
		const get_hostfxr_parameters* pParameters);

	ScriptEngine& ScriptEngine::Get()
	{
		static ScriptEngine s_Instance;
		return s_Instance;
	}

	bool ScriptEngine::Initialize(
		const VspString& sAssemblyPath,
		const VspString& sRuntimeConfigPath,
		const VspString& sDotNetRootPath,
		VspString& outErrorText)
	{
		outErrorText = nullptr;

		if (IsInitialized())
		{
			outErrorText = "ScriptEngine is already initialized.";
			return false;
		}

		if (!LoadNetHostLibrary(sDotNetRootPath, outErrorText))
		{
			return false;
		}

		if (!LoadHostFxrLibrary(outErrorText))
		{
			return false;
		}

		if (!InitializeHostFxrRuntime(sRuntimeConfigPath, outErrorText))
		{
			return false;
		}

		if (!LoadManagedEntryPoints(sAssemblyPath, outErrorText))
		{
			return false;
		}

		LOG_INFO(kLogTag, "CoreCLR host initialized (assembly: {})", sAssemblyPath.ToStdString());
		return true;
	}

	void ScriptEngine::Shutdown()
	{
		// Destroy managed instances first: the runtime must still be usable.
		for (size_t nInstanceIndex = 0; nInstanceIndex < m_ScriptInstanceIds.GetSize(); ++nInstanceIndex)
		{
			DestroyScriptInstance(m_ScriptInstanceIds[nInstanceIndex]);
		}
		m_ScriptInstanceIds.Clear();

		// Close the hostfxr context (CoreCLR itself stays loaded per-process).
		if (m_hHostFxrLibrary != nullptr && m_RuntimeContext != nullptr)
		{
			auto pCloseFn = reinterpret_cast<hostfxr_close_fn>(
				GetProcAddress(m_hHostFxrLibrary, "hostfxr_close"));
			if (pCloseFn != nullptr)
			{
				pCloseFn(m_RuntimeContext);
			}
			m_RuntimeContext = nullptr;
		}

		if (m_hHostFxrLibrary != nullptr)
		{
			FreeLibrary(m_hHostFxrLibrary);
			m_hHostFxrLibrary = nullptr;
		}

		if (m_hNetHostLibrary != nullptr)
		{
			FreeLibrary(m_hNetHostLibrary);
			m_hNetHostLibrary = nullptr;
		}

		m_LoadAssemblyFunction = nullptr;
		m_BridgeFunctions = BridgeFunctions();
		LOG_INFO(kLogTag, "CoreCLR host shut down.");
	}

	// -------------------------------------------------------------------------
	// Hosting bootstrap
	// -------------------------------------------------------------------------

	bool ScriptEngine::LoadNetHostLibrary(const VspString& sDotNetRootPath, VspString& outErrorText)
	{
		// nethost.dll ships next to the runtime in <DotNetRoot>/host/nethost.dll.
		VspString sNetHostPath = sDotNetRootPath + "\\host\\nethost.dll";

		m_hNetHostLibrary = LoadLibraryW(sNetHostPath.ToWideText().GetData());
		if (m_hNetHostLibrary == nullptr)
		{
			outErrorText = "Failed to load nethost.dll from " + sNetHostPath;
			return false;
		}
		return true;
	}

	bool ScriptEngine::LoadHostFxrLibrary(VspString& outErrorText)
	{
		auto pGetHostFxrPathFn = reinterpret_cast<GetHostFxrPathFn>(
			GetProcAddress(m_hNetHostLibrary, "get_hostfxr_path"));
		if (pGetHostFxrPathFn == nullptr)
		{
			outErrorText = "nethost.dll does not export get_hostfxr_path.";
			return false;
		}

		wchar_t sHostFxrPathBuffer[2048] = {};
		size_t nBufferSize = 2048;

		// DOTNET_ROOT is set by the game engine before this call, so hostfxr
		// is located next to the shipped runtime.
		const int32_t nResult = pGetHostFxrPathFn(sHostFxrPathBuffer, &nBufferSize, nullptr);
		if (nResult != 0)
		{
			outErrorText = "get_hostfxr_path failed with error code 0x" + FormatHex(nResult);
			return false;
		}

		m_hHostFxrLibrary = LoadLibraryW(sHostFxrPathBuffer);
		if (m_hHostFxrLibrary == nullptr)
		{
			outErrorText = "Failed to load hostfxr.dll from " + VspString(sHostFxrPathBuffer);
			return false;
		}
		return true;
	}

	bool ScriptEngine::InitializeHostFxrRuntime(const VspString& sRuntimeConfigPath, VspString& outErrorText)
	{
		auto pInitializeForRuntimeConfigFn = reinterpret_cast<hostfxr_initialize_for_runtime_config_fn>(
			GetProcAddress(m_hHostFxrLibrary, "hostfxr_initialize_for_runtime_config"));
		auto pGetRuntimeDelegateFn = reinterpret_cast<hostfxr_get_runtime_delegate_fn>(
			GetProcAddress(m_hHostFxrLibrary, "hostfxr_get_runtime_delegate"));
		if (pInitializeForRuntimeConfigFn == nullptr || pGetRuntimeDelegateFn == nullptr)
		{
			outErrorText = "hostfxr.dll is missing required exports.";
			return false;
		}

		int32_t nResult = pInitializeForRuntimeConfigFn(
			sRuntimeConfigPath.ToWideText().GetData(), nullptr, &m_RuntimeContext);
		if (nResult != 0 || m_RuntimeContext == nullptr)
		{
			outErrorText = "hostfxr_initialize_for_runtime_config failed with error code 0x" + FormatHex(nResult);
			return false;
		}

		nResult = pGetRuntimeDelegateFn(
			m_RuntimeContext, hdt_load_assembly_and_get_function_pointer,
			reinterpret_cast<void**>(&m_LoadAssemblyFunction));
		if (nResult != 0 || m_LoadAssemblyFunction == nullptr)
		{
			outErrorText = "hostfxr_get_runtime_delegate failed with error code 0x" + FormatHex(nResult);
			return false;
		}
		return true;
	}

	bool ScriptEngine::LoadManagedEntryPoints(const VspString& sAssemblyPath, VspString& outErrorText)
	{
		ArrayList<wchar_t> assemblyPathUtf16 = sAssemblyPath.ToWideText();
		const wchar_t* pAssemblyPath = assemblyPathUtf16.GetData();

		auto FetchEntryPoint = [&](const wchar_t* pMethodName, void*& pFunction)
		{
			return FetchManagedEntryPoint(
				m_LoadAssemblyFunction,
				pAssemblyPath,
				k_sManagedBridgeTypeName,
				pMethodName,
				&pFunction,
				outErrorText);
		};

		void* pFunction = nullptr;

		pFunction = nullptr;
		if (!FetchEntryPoint(L"CreateInstance", pFunction)) return false;
		m_BridgeFunctions.CreateInstance = reinterpret_cast<ManagedBridgeDetail::CreateInstanceFn>(pFunction);

		pFunction = nullptr;
		if (!FetchEntryPoint(L"CreateAllScripts", pFunction)) return false;
		m_BridgeFunctions.CreateAllScripts = reinterpret_cast<ManagedBridgeDetail::CreateAllScriptsFn>(pFunction);

		pFunction = nullptr;
		if (!FetchEntryPoint(L"FreeIntArray", pFunction)) return false;
		m_BridgeFunctions.FreeIntArray = reinterpret_cast<ManagedBridgeDetail::FreeIntArrayFn>(pFunction);

		pFunction = nullptr;
		if (!FetchEntryPoint(L"CallOnInit", pFunction)) return false;
		m_BridgeFunctions.CallOnInit = reinterpret_cast<ManagedBridgeDetail::CallOnInitFn>(pFunction);

		pFunction = nullptr;
		if (!FetchEntryPoint(L"CallOnStart", pFunction)) return false;
		m_BridgeFunctions.CallOnStart = reinterpret_cast<ManagedBridgeDetail::CallOnStartFn>(pFunction);

		pFunction = nullptr;
		if (!FetchEntryPoint(L"CallOnUpdate", pFunction)) return false;
		m_BridgeFunctions.CallOnUpdate = reinterpret_cast<ManagedBridgeDetail::CallOnUpdateFn>(pFunction);

		pFunction = nullptr;
		if (!FetchEntryPoint(L"DestroyInstance", pFunction)) return false;
		m_BridgeFunctions.DestroyInstance = reinterpret_cast<ManagedBridgeDetail::DestroyInstanceFn>(pFunction);

		return true;
	}

	bool ScriptEngine::FetchManagedEntryPoint(
		load_assembly_and_get_function_pointer_fn pLoadAssemblyFunction,
		const wchar_t* pAssemblyPathUtf16,
		const wchar_t* pTypeNameUtf16,
		const wchar_t* pMethodNameUtf16,
		void** ppOutFunction,
		VspString& outErrorText)
	{
		const int32_t nResult = pLoadAssemblyFunction(
			pAssemblyPathUtf16,
			pTypeNameUtf16,
			pMethodNameUtf16,
			UNMANAGEDCALLERSONLY_METHOD,   // entry points are [UnmanagedCallersOnly]
			nullptr,
			ppOutFunction);

		if (nResult != 0 || *ppOutFunction == nullptr)
		{
			VspString sMethodName(pMethodNameUtf16);
			outErrorText = "Failed to resolve managed entry point '" + sMethodName + "' (error code 0x" + FormatHex(nResult) + ")";
			return false;
		}
		return true;
	}

	// -------------------------------------------------------------------------
	// Script instance management (C++ host is the caller)
	// -------------------------------------------------------------------------

	bool ScriptEngine::CreateAllScriptInstances(VspString& outErrorText)
	{
		if (!IsInitialized() || m_BridgeFunctions.CreateAllScripts == nullptr)
		{
			outErrorText = "ScriptEngine is not initialized.";
			return false;
		}

		int32_t nInstanceCount = 0;
		int32_t* pInstanceIds = m_BridgeFunctions.CreateAllScripts(&nInstanceCount);
		if (pInstanceIds == nullptr || nInstanceCount <= 0)
		{
			outErrorText = "No ScriptBehaviour types found in the game assembly.";
			return false;
		}

		for (int32_t nIndex = 0; nIndex < nInstanceCount; ++nIndex)
		{
			m_ScriptInstanceIds.Add(static_cast<ScriptInstanceId>(pInstanceIds[nIndex]));
			LOG_INFO(kLogTag, "Created script instance with InstanceID {}", pInstanceIds[nIndex]);
		}

		m_BridgeFunctions.FreeIntArray(pInstanceIds);
		return true;
	}

	ScriptInstanceId ScriptEngine::CreateScriptInstance(const VspString& sTypeName)
	{
		if (!IsInitialized() || m_BridgeFunctions.CreateInstance == nullptr)
		{
			return 0;
		}

		const int32_t nInstanceId = m_BridgeFunctions.CreateInstance(sTypeName.ToWideText().GetData());
		if (nInstanceId <= 0)
		{
			return 0;
		}

		m_ScriptInstanceIds.Add(static_cast<ScriptInstanceId>(nInstanceId));
		return static_cast<ScriptInstanceId>(nInstanceId);
	}

	void ScriptEngine::CallScriptInit(ScriptInstanceId uInstanceId)
	{
		if (uInstanceId != 0 && m_BridgeFunctions.CallOnInit != nullptr)
		{
			m_BridgeFunctions.CallOnInit(static_cast<int32_t>(uInstanceId));
		}
	}

	void ScriptEngine::CallScriptStart(ScriptInstanceId uInstanceId)
	{
		if (uInstanceId != 0 && m_BridgeFunctions.CallOnStart != nullptr)
		{
			m_BridgeFunctions.CallOnStart(static_cast<int32_t>(uInstanceId));
		}
	}

	void ScriptEngine::CallScriptUpdate(ScriptInstanceId uInstanceId)
	{
		if (uInstanceId != 0 && m_BridgeFunctions.CallOnUpdate != nullptr)
		{
			m_BridgeFunctions.CallOnUpdate(static_cast<int32_t>(uInstanceId));
		}
	}

	void ScriptEngine::DestroyScriptInstance(ScriptInstanceId uInstanceId)
	{
		if (uInstanceId == 0 || m_BridgeFunctions.DestroyInstance == nullptr)
		{
			return;
		}

		m_BridgeFunctions.DestroyInstance(static_cast<int32_t>(uInstanceId));

		for (size_t nIndex = 0; nIndex < m_ScriptInstanceIds.GetSize(); ++nIndex)
		{
			if (m_ScriptInstanceIds[nIndex] == uInstanceId)
			{
				m_ScriptInstanceIds.RemoveAt(nIndex);
				break;
			}
		}
	}

	void ScriptEngine::UpdateAllScripts()
	{
		if (!IsInitialized())
		{
			return;
		}

		for (size_t nIndex = 0; nIndex < m_ScriptInstanceIds.GetSize(); ++nIndex)
		{
			CallScriptUpdate(m_ScriptInstanceIds[nIndex]);
		}
	}
}
