#pragma once

#include <nethost.h>
#include <hostfxr.h>
#include <coreclr_delegates.h>

#include "Core/String/VspString.h"

namespace Vsp
{
    namespace Detail
    {
        using CreateInstanceFn      = int   (*)(const char* typeName);
        using CallOnInitFn          = void  (*)(int id);
        using CallOnUpdateFn        = void  (*)(int id);
        using DestroyInstanceFn     = void  (*)(int id);
        using CreateAllScriptsFn    = int*  (*)(int* outCount);
        using FreeIntArrayFn        = void  (*)(void* ptr);
    }

    struct BridgeFunctions
    {
        Detail::CreateInstanceFn    CreateInst;
        Detail::CallOnInitFn        OnInit;
        Detail::CallOnUpdateFn      OnUpdate;
        Detail::DestroyInstanceFn   DestroyInst;
        Detail::CreateAllScriptsFn  CreateAllScripts;
        Detail::FreeIntArrayFn      FreeIntArray;
    };


    bool GetBridgeFunctions(const VspString& assemblyPath, BridgeFunctions& bridge)
    {

    }

    hostfxr_handle g_RuntimeHandle = nullptr;
    load_assembly_and_get_function_pointer_fn g_LoadAssemblyFn = nullptr;
}
