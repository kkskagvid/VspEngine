#pragma once

#ifdef _MSC_VER
    #define VSP_COMPILER_MSVC 1
    #ifdef _DEBUG
        #define VSP_ENGINE_DEBUG 1
    #else
        #define VSP_ENGINE_DEBUG 0
    #endif
#elif defined(__GNUC__)
    #define VSP_COMPILER_GCC 1
    #ifdef (DEBUG)
        #define VSP_ENGINE_DEBUG 1
    #else
        #define VSP_ENGINE_DEBUG 0
    #endif
#elif defined(__clang__)
    #define VSP_COMPILER_CLANG 1
    #ifdef (DEBUG)
        #define VSP_ENGINE_DEBUG 1
    #else
        #define VSP_ENGINE_DEBUG 0
    #endif
#endif

#if defined(_WIN32) || defined(_WIN64)
    #define VSP_PLATFORM_WINDOWS 1
#elif defined(__linux__)
    #define VSP_PLATFORM_LINUX 1
#elif defined(__APPLE__) && defined(__MACH__)
    #define VSP_PLATFORM_MACOS 1
#elif defined(__ANDROID__)
    #define VSP_PLATFORM_ANDROID 1
#endif
