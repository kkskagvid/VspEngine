#pragma once

// The export marker of the interop surface: every Vsp*_* function managed code
// P/Invokes is declared with it. The spelling is the compiler's business, so the
// macro is what keeps the export list free of a platform keyword.
#if defined(_MSC_VER)
	#define CSHARP_EXPORT extern "C" __declspec(dllexport)
#else
	#define CSHARP_EXPORT extern "C" __attribute__((visibility("default")))
#endif