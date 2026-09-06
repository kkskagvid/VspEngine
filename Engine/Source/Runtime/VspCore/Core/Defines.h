#pragma once

#ifdef VSP_PLATFORM_WINDOWS
	#ifdef VSP_BUILD_DLL
		#define RUNTIME_API __declspec(dllexport)
	#else
		#define RUNTIME_API __declspec(dllimport)
	#endif
#else
	#define RUNTIME_API
#endif

#if VSP_ENGINE_DEBUG
	#if VSP_PLATFORM_WINDOWS
		#define DEBUG_BREAK() __debugbreak()
	#else
		#include <signal.h>
		#define DEBUG_BREAK() raise(SIGTRAP)
	#endif
#else
	#define DEBUG_BREAK()
#endif

#if VSP_COMPILER_MSVC
	#define INLINE              inline
	#define FORCEINLINE        __forceinline
	#define NOINLINE           __declspec(noinline)
#else
	#define INLINE              inline
	#define FORCEINLINE        __attribute__((always_inline))
	#define NOINLINE           __attribute__((noinline))
#endif

#define BIT(x) (1 << x)

