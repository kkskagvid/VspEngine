#pragma once

// Clang is tested BEFORE GCC: Clang defines __GNUC__ as well, so a GCC-first
// chain would never reach the Clang branch (it used to be unreachable).
#if defined(_MSC_VER)
    #define VSP_COMPILER_MSVC 1
    #if defined(_DEBUG)
        #define VSP_ENGINE_DEBUG 1
    #else
        #define VSP_ENGINE_DEBUG 0
    #endif
#elif defined(__clang__)
    #define VSP_COMPILER_CLANG 1
    #if defined(DEBUG)
        #define VSP_ENGINE_DEBUG 1
    #else
        #define VSP_ENGINE_DEBUG 0
    #endif
#elif defined(__GNUC__)
    #define VSP_COMPILER_GCC 1
    #if defined(DEBUG)
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
