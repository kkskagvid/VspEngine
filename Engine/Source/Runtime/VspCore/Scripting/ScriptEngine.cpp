#include "RuntimePCH.h"

#include <cstdio>

#include "Classes/Scene.h"
#include "Common/PlatformMisc.h"
#include "Core/EngineServices.h"
#include "Core/Logging/Log.h"
#include "Scripting/ScriptEngine.h"

namespace Vsp
{
	static constexpr const char* kLogTag = "ScriptEngine";

	// The managed bridge type (assembly-qualified) hosting the entry points.
	// It lives in the engine's managed runtime assembly (VspEngine.dll).
	static constexpr const wchar_t* k_sManagedBridgeTypeName = L"VspEngine.NativeBridge, VspEngine";

	// Number of entry points the bridge hands back in one table call.
	static constexpr uint32 k_nManagedBridgeFunctionCount = 10;

	// Formats a 32-bit value as an 8-digit hexadecimal string for error text.
	static VspString FormatHex(int32 nValue)
	{
		char sBuffer[32];
		snprintf(sBuffer, sizeof(sBuffer), "%08X", static_cast<uint32>(nValue));
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
		// The registry owns this service: it is created here on first use,
		// reports a lookup from any thread but the one that created it, and is
		// destroyed explicitly by EngineServices::ShutdownAll().
		return EngineServices::GetService<ScriptEngine>("ScriptEngine");
	}

	bool ScriptEngine::Initialize(
		const VspString& sEngineAssemblyPath,
		const VspString& sGameAssemblyPath,
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

		// Bridge entry points live in the engine's managed runtime assembly.
		if (!LoadManagedEntryPoints(sEngineAssemblyPath, outErrorText))
		{
			return false;
		}

		// The game Assembly holds the user scripts (the engine loads it here).
		if (!LoadGameAssembly(sGameAssemblyPath, outErrorText))
		{
			return false;
		}

		LOG_INFO(kLogTag, "CoreCLR host initialized (engine assembly: {}, game assembly: {}).",
			sEngineAssemblyPath.GetData(), sGameAssemblyPath.GetData());
		return true;
	}

	void ScriptEngine::Shutdown()
	{
		// Destroy managed instances first: the runtime must still be usable.
		for (size_t nInstanceIndex = 0; nInstanceIndex < m_ScriptInstances.GetSize(); ++nInstanceIndex)
		{
			DestroyScriptInstance(m_ScriptInstances[nInstanceIndex].uInstanceId);
		}
		m_ScriptInstances.Clear();

		// The render pipeline owns graphics resources; let it release them
		// while the backend is still alive.
		ReleaseRenderPipeline();

		// Close the hostfxr context (CoreCLR itself stays loaded per-process).
		if (m_hHostFxrLibrary != nullptr && m_RuntimeContext != nullptr)
		{
			auto pCloseFn = reinterpret_cast<hostfxr_close_fn>(
				PlatformMisc::GetDynamicLibraryFunction(m_hHostFxrLibrary, "hostfxr_close"));
			if (pCloseFn != nullptr)
			{
				pCloseFn(m_RuntimeContext);
			}
			m_RuntimeContext = nullptr;
		}

		if (m_hHostFxrLibrary != nullptr)
		{
			PlatformMisc::UnloadDynamicLibrary(m_hHostFxrLibrary);
			m_hHostFxrLibrary = nullptr;
		}

		if (m_hNetHostLibrary != nullptr)
		{
			PlatformMisc::UnloadDynamicLibrary(m_hNetHostLibrary);
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

		m_hNetHostLibrary = PlatformMisc::LoadDynamicLibrary(sNetHostPath);
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
			PlatformMisc::GetDynamicLibraryFunction(m_hNetHostLibrary, "get_hostfxr_path"));
		if (pGetHostFxrPathFn == nullptr)
		{
			outErrorText = "nethost.dll does not export get_hostfxr_path.";
			return false;
		}

		wchar_t sHostFxrPathBuffer[2048] = {};
		size_t nBufferSize = 2048;

		// DOTNET_ROOT is set by the game engine before this call, so hostfxr
		// is located next to the shipped runtime.
		const int32 nResult = pGetHostFxrPathFn(sHostFxrPathBuffer, &nBufferSize, nullptr);
		if (nResult != 0)
		{
			outErrorText = "get_hostfxr_path failed with error code 0x" + FormatHex(nResult);
			return false;
		}

		m_hHostFxrLibrary = PlatformMisc::LoadDynamicLibrary(VspString(sHostFxrPathBuffer));
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
			PlatformMisc::GetDynamicLibraryFunction(m_hHostFxrLibrary, "hostfxr_initialize_for_runtime_config"));
		auto pGetRuntimeDelegateFn = reinterpret_cast<hostfxr_get_runtime_delegate_fn>(
			PlatformMisc::GetDynamicLibraryFunction(m_hHostFxrLibrary, "hostfxr_get_runtime_delegate"));
		if (pInitializeForRuntimeConfigFn == nullptr || pGetRuntimeDelegateFn == nullptr)
		{
			outErrorText = "hostfxr.dll is missing required exports.";
			return false;
		}

		int32 nResult = pInitializeForRuntimeConfigFn(
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

	bool ScriptEngine::LoadManagedEntryPoints(const VspString& sEngineAssemblyPath, VspString& outErrorText)
	{
		ArrayList<wchar_t> assemblyPathUtf16 = sEngineAssemblyPath.ToWideText();
		const wchar_t* pAssemblyPath = assemblyPathUtf16.GetData();

		// Exactly ONE load_assembly_and_get_function_pointer call loads
		// VspEngine.dll. Every additional call would load another COPY of the
		// assembly (each copy carries its own static state), so the single
		// loaded copy hands the whole entry-point table back in one call.
		void* pFunction = nullptr;
		if (!FetchManagedEntryPoint(
			m_LoadAssemblyFunction,
			pAssemblyPath,
			k_sManagedBridgeTypeName,
			L"GetBridgeFunctionTable",
			&pFunction,
			outErrorText))
		{
			return false;
		}
		m_BridgeFunctions.GetBridgeFunctionTable =
			reinterpret_cast<ManagedBridgeDetail::GetBridgeFunctionTableFn>(pFunction);

		// Slot order mirrors NativeBridge.GetBridgeFunctionTable:
		//   0 CreateInstance, 1 GetScriptTypeCount, 2 GetScriptTypeName,
		//   3 CallOnInit, 4 CallOnStart, 5 CallOnUpdate, 6 DestroyInstance,
		//   7 CallRenderFrame, 8 LoadGameAssembly, 9 ReleaseRenderPipeline.
		void* pFunctionTable[k_nManagedBridgeFunctionCount] = {};
		m_BridgeFunctions.GetBridgeFunctionTable(pFunctionTable);

		m_BridgeFunctions.CreateInstance = reinterpret_cast<ManagedBridgeDetail::CreateInstanceFn>(pFunctionTable[0]);
		m_BridgeFunctions.GetScriptTypeCount = reinterpret_cast<ManagedBridgeDetail::GetScriptTypeCountFn>(pFunctionTable[1]);
		m_BridgeFunctions.GetScriptTypeName = reinterpret_cast<ManagedBridgeDetail::GetScriptTypeNameFn>(pFunctionTable[2]);
		m_BridgeFunctions.CallOnInit = reinterpret_cast<ManagedBridgeDetail::CallOnInitFn>(pFunctionTable[3]);
		m_BridgeFunctions.CallOnStart = reinterpret_cast<ManagedBridgeDetail::CallOnStartFn>(pFunctionTable[4]);
		m_BridgeFunctions.CallOnUpdate = reinterpret_cast<ManagedBridgeDetail::CallOnUpdateFn>(pFunctionTable[5]);
		m_BridgeFunctions.DestroyInstance = reinterpret_cast<ManagedBridgeDetail::DestroyInstanceFn>(pFunctionTable[6]);
		m_BridgeFunctions.CallRenderFrame = reinterpret_cast<ManagedBridgeDetail::CallRenderFrameFn>(pFunctionTable[7]);
		m_BridgeFunctions.LoadGameAssembly = reinterpret_cast<ManagedBridgeDetail::LoadGameAssemblyFn>(pFunctionTable[8]);
		m_BridgeFunctions.ReleaseRenderPipeline = reinterpret_cast<ManagedBridgeDetail::ReleaseRenderPipelineFn>(pFunctionTable[9]);

		if (m_BridgeFunctions.CreateInstance == nullptr ||
			m_BridgeFunctions.GetScriptTypeCount == nullptr ||
			m_BridgeFunctions.GetScriptTypeName == nullptr ||
			m_BridgeFunctions.CallOnInit == nullptr ||
			m_BridgeFunctions.CallOnStart == nullptr ||
			m_BridgeFunctions.CallOnUpdate == nullptr ||
			m_BridgeFunctions.DestroyInstance == nullptr ||
			m_BridgeFunctions.CallRenderFrame == nullptr ||
			m_BridgeFunctions.LoadGameAssembly == nullptr ||
			m_BridgeFunctions.ReleaseRenderPipeline == nullptr)
		{
			outErrorText = "The managed bridge returned an incomplete entry-point table.";
			return false;
		}
		return true;
	}

	bool ScriptEngine::LoadGameAssembly(const VspString& sGameAssemblyPath, VspString& outErrorText)
	{
		if (!IsInitialized() || m_BridgeFunctions.LoadGameAssembly == nullptr)
		{
			outErrorText = "ScriptEngine is not initialized.";
			return false;
		}

		// The managed bridge loads the game Assembly (Assembly.dll) into its
		// own load context - the context that holds the single VspEngine.dll
		// copy - so the bridge's ScriptBehaviour type and the scripts' base
		// type are the same type.
		const int32 nResult = m_BridgeFunctions.LoadGameAssembly(
			sGameAssemblyPath.ToWideText().GetData());
		if (nResult == 0)
		{
			outErrorText = "Failed to load the game Assembly: " + sGameAssemblyPath;
			return false;
		}
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
		const int32 nResult = pLoadAssemblyFunction(
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

	ScriptEngine::ScriptInstanceEntry* ScriptEngine::FindInstanceEntry(ScriptInstanceId uInstanceId)
	{
		for (size_t nIndex = 0; nIndex < m_ScriptInstances.GetSize(); ++nIndex)
		{
			if (m_ScriptInstances[nIndex].uInstanceId == uInstanceId)
			{
				return &m_ScriptInstances[nIndex];
			}
		}
		return nullptr;
	}

	const ScriptEngine::ScriptInstanceEntry* ScriptEngine::FindInstanceEntry(ScriptInstanceId uInstanceId) const
	{
		for (size_t nIndex = 0; nIndex < m_ScriptInstances.GetSize(); ++nIndex)
		{
			if (m_ScriptInstances[nIndex].uInstanceId == uInstanceId)
			{
				return &m_ScriptInstances[nIndex];
			}
		}
		return nullptr;
	}

	bool ScriptEngine::CreateAllScriptInstances(VspString& outErrorText)
	{
		if (!IsInitialized() ||
			m_BridgeFunctions.GetScriptTypeCount == nullptr ||
			m_BridgeFunctions.GetScriptTypeName == nullptr)
		{
			outErrorText = "ScriptEngine is not initialized.";
			return false;
		}

		// Ask the managed bridge for every concrete ScriptBehaviour type, then
		// create one instance of each. The InstanceID is generated here.
		const int32 nScriptTypeCount = m_BridgeFunctions.GetScriptTypeCount();
		if (nScriptTypeCount <= 0)
		{
			outErrorText = "No ScriptBehaviour types found in the game assembly.";
			return false;
		}

		wchar_t sTypeNameBuffer[512];
		for (int32 nTypeIndex = 0; nTypeIndex < nScriptTypeCount; ++nTypeIndex)
		{
			m_BridgeFunctions.GetScriptTypeName(nTypeIndex, sTypeNameBuffer, 512);
			const ScriptInstanceId uInstanceId = CreateScriptInstance(VspString(sTypeNameBuffer));
			if (uInstanceId == 0)
			{
				LOG_WARNING(kLogTag, "Failed to create script instance for type '{}'.", VspString(sTypeNameBuffer).GetData());
			}
		}

		return GetScriptInstanceCount() > 0;
	}

	ScriptInstanceId ScriptEngine::CreateScriptInstance(const VspString& sTypeName)
	{
		if (!IsInitialized() || m_BridgeFunctions.CreateInstance == nullptr)
		{
			return 0;
		}

		// The C++ host owns InstanceID generation (non-negative, 1-based).
		const ScriptInstanceId uInstanceId = m_nNextInstanceId++;

		// The script's native data lives in the scene: every script instance
		// owns a GameObject (with its Transform) and the script Component that
		// is attached to it. The managed ScriptBehaviour only stores the three
		// handles, so nothing but the handle set crosses the boundary.
		Scene& scene = Scene::Get();
		const NativeObjectHandle uGameObjectHandle = scene.CreateGameObject(sTypeName);
		if (uGameObjectHandle == k_nInvalidObjectHandle)
		{
			LOG_ERROR(kLogTag, "Failed to create the game object of script type '{}'.", sTypeName.GetData());
			return 0;
		}

		const NativeObjectHandle uComponentHandle = scene.CreateComponent(uGameObjectHandle, ComponentKind::Script);
		if (uComponentHandle == k_nInvalidObjectHandle)
		{
			scene.DestroyGameObject(uGameObjectHandle);
			return 0;
		}

		Component* pComponent = scene.FindComponent(uComponentHandle);
		if (pComponent != nullptr)
		{
			pComponent->SetScriptTypeName(sTypeName);
		}

		const NativeObjectHandle uTransformHandle = scene.FindGameObjectTransformHandle(uGameObjectHandle);

		void* pManagedHandle = m_BridgeFunctions.CreateInstance(
			static_cast<int32>(uInstanceId),
			uGameObjectHandle,
			uTransformHandle,
			uComponentHandle,
			sTypeName.ToWideText().GetData());
		if (pManagedHandle == nullptr)
		{
			scene.DestroyGameObject(uGameObjectHandle);
			return 0;
		}

		ScriptInstanceEntry entry;
		entry.uInstanceId = uInstanceId;
		entry.pManagedHandle = pManagedHandle;
		entry.uGameObjectHandle = uGameObjectHandle;
		entry.uTransformHandle = uTransformHandle;
		entry.uComponentHandle = uComponentHandle;
		m_ScriptInstances.Add(entry);

		LOG_INFO(kLogTag, "Created script instance with InstanceID {} (game object handle {}).",
			uInstanceId, uGameObjectHandle);
		return uInstanceId;
	}

	void ScriptEngine::CallScriptInit(ScriptInstanceId uInstanceId)
	{
		const ScriptInstanceEntry* pEntry = FindInstanceEntry(uInstanceId);
		if (pEntry != nullptr && pEntry->pManagedHandle != nullptr && m_BridgeFunctions.CallOnInit != nullptr)
		{
			m_BridgeFunctions.CallOnInit(pEntry->pManagedHandle);
		}
	}

	void ScriptEngine::CallScriptStart(ScriptInstanceId uInstanceId)
	{
		const ScriptInstanceEntry* pEntry = FindInstanceEntry(uInstanceId);
		if (pEntry != nullptr && pEntry->pManagedHandle != nullptr && m_BridgeFunctions.CallOnStart != nullptr)
		{
			m_BridgeFunctions.CallOnStart(pEntry->pManagedHandle);
		}
	}

	void ScriptEngine::CallScriptUpdate(ScriptInstanceId uInstanceId)
	{
		const ScriptInstanceEntry* pEntry = FindInstanceEntry(uInstanceId);
		if (pEntry != nullptr && pEntry->pManagedHandle != nullptr && m_BridgeFunctions.CallOnUpdate != nullptr)
		{
			m_BridgeFunctions.CallOnUpdate(pEntry->pManagedHandle);
		}
	}

	void ScriptEngine::DestroyScriptInstance(ScriptInstanceId uInstanceId)
	{
		const ScriptInstanceEntry* pEntry = FindInstanceEntry(uInstanceId);
		if (pEntry == nullptr)
		{
			return;
		}

		if (m_BridgeFunctions.DestroyInstance != nullptr)
		{
			m_BridgeFunctions.DestroyInstance(pEntry->pManagedHandle);
		}

		// Releasing the game object also releases its transform and the script
		// component attached to it.
		if (pEntry->uGameObjectHandle != k_nInvalidObjectHandle)
		{
			Scene::Get().DestroyGameObject(pEntry->uGameObjectHandle);
		}

		for (size_t nIndex = 0; nIndex < m_ScriptInstances.GetSize(); ++nIndex)
		{
			if (m_ScriptInstances[nIndex].uInstanceId == uInstanceId)
			{
				m_ScriptInstances.RemoveAt(nIndex);
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

		for (size_t nIndex = 0; nIndex < m_ScriptInstances.GetSize(); ++nIndex)
		{
			CallScriptUpdate(m_ScriptInstances[nIndex].uInstanceId);
		}
	}

	void ScriptEngine::CallRenderFrame()
	{
		if (!IsInitialized() || m_BridgeFunctions.CallRenderFrame == nullptr)
		{
			return;
		}

		// The managed frame driver opens the frame (render pass, clear, sky,
		// interface), lets the active pipeline record its passes inside it and
		// records every graphics command through the wrapped graphics API; the
		// backend plays the recorded list back when it renders the frame.
		m_BridgeFunctions.CallRenderFrame();
	}

	void ScriptEngine::ReleaseRenderPipeline()
	{
		if (!IsInitialized() || m_BridgeFunctions.ReleaseRenderPipeline == nullptr)
		{
			return;
		}

		// The pipeline owns graphics resources created through the wrapped
		// graphics API; releasing them here keeps the resource teardown inside
		// the managed pipeline instead of the backend's shutdown path.
		m_BridgeFunctions.ReleaseRenderPipeline();
	}
}
