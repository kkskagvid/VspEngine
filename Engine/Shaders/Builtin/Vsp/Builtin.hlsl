// ---------------------------------------------------------------------------
// Vsp/Builtin.hlsl - the whole HLSL builtin library in one include.
//
//     #include <Vsp/Builtin.hlsl>
//
// brings in the constants and color helpers (Vsp/Common.hlsl), the transform
// helpers (Vsp/Transform.hlsl), the bindless texture access
// (Vsp/Texture.hlsl), the normal-map helpers (Vsp/Normal.hlsl) and the
// lighting models (Vsp/Lighting.hlsl).
//
// Including one file directly works just as well and keeps a shader's
// dependencies visible:
//
//     #include <Vsp/Lighting.hlsl>
//
// HLSLCC resolves <...> includes against its builtin directory, which is
// staged next to the compiler (Engine/Shaders/Builtin) and can be pointed
// elsewhere with --builtin-directory.
// ---------------------------------------------------------------------------

#ifndef VSP_BUILTIN_INCLUDED
#define VSP_BUILTIN_INCLUDED

#include <Vsp/Common.hlsl>
#include <Vsp/Transform.hlsl>
#include <Vsp/Texture.hlsl>
#include <Vsp/Normal.hlsl>
#include <Vsp/Lighting.hlsl>

#endif // VSP_BUILTIN_INCLUDED
