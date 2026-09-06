#pragma once

// Precompiled header for VspCore.
//
// Deliberately kept free of standard-library includes: every translation unit
// and header declares the headers it actually uses. This keeps include
// dependencies visible and the PCH small and stable.
//
// Third-party defines (e.g. U_STATIC_IMPLEMENTATION for ICU, FMT_HEADER_ONLY
// for fmt) have been removed together with their consumers; reinstate them
// only together with the libraries that need them.
