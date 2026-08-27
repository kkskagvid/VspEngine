#pragma once

#include <cstdint>

#include <nethost.h>
#include <hostfxr.h>
#include <coreclr_delegates.h>

namespace Vsp
{
	// -------------------------------------------------------------------------
	// BridgeFunctions
	// -------------------------------------------------------------------------
	// Raw function pointers into the managed bridge (VspEngine.NativeBridge in
	// VspPlayer.dll), fetched through hostfxr's
	// load_assembly_and_get_function_pointer. Every managed entry point is
	// marked [UnmanagedCallersOnly], so the pointers follow the platform
	// calling convention (STDMETHODCALLTYPE == the native default on x64).
	// -------------------------------------------------------------------------

	namespace ManagedBridgeDetail
	{
		using CreateInstanceFn   = int32_t  (STDMETHODCALLTYPE*)(const char_t* pTypeNameUtf16);
		using CreateAllScriptsFn = int32_t* (STDMETHODCALLTYPE*)(int32_t* pOutCount);
		using FreeIntArrayFn     = void     (STDMETHODCALLTYPE*)(void* pArray);
		using CallOnInitFn       = void     (STDMETHODCALLTYPE*)(int32_t nInstanceId);
		using CallOnStartFn      = void     (STDMETHODCALLTYPE*)(int32_t nInstanceId);
		using CallOnUpdateFn     = void     (STDMETHODCALLTYPE*)(int32_t nInstanceId);
		using DestroyInstanceFn  = void     (STDMETHODCALLTYPE*)(int32_t nInstanceId);
	}

	struct BridgeFunctions
	{
		ManagedBridgeDetail::CreateInstanceFn   CreateInstance = nullptr;
		ManagedBridgeDetail::CreateAllScriptsFn CreateAllScripts = nullptr;
		ManagedBridgeDetail::FreeIntArrayFn     FreeIntArray = nullptr;
		ManagedBridgeDetail::CallOnInitFn       CallOnInit = nullptr;
		ManagedBridgeDetail::CallOnStartFn      CallOnStart = nullptr;
		ManagedBridgeDetail::CallOnUpdateFn     CallOnUpdate = nullptr;
		ManagedBridgeDetail::DestroyInstanceFn  DestroyInstance = nullptr;
	};
}
