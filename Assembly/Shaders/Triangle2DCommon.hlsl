// ---------------------------------------------------------------------------
// Shared declarations of the demo shader.
//
// The Pass block of Triangle2D.vsf includes this file, so the two entry points
// in the shader file stay readable and the declarations can be shared with
// other passes or shaders.
//
// The demo draws in 3D: its vertices carry a world-space position, a normal, a
// vertex color and a texture coordinate, and the vertex stage turns them into
// clip space with the camera the render pipeline handed to the frame. The "2D"
// part of the demo - the colored triangle - is one more mesh in that 3D scene.
//
// NOTE: no resource names a binding. The ENGINE decides where every resource
// lives and HLSLCC applies that decision to the compiled SPIR-V, so a shader
// only says WHICH resource it uses, never where it sits.
//
// The Vulkan HLSL namespace (vk::) and the VSP_VK_* attribute shorthands are
// injected by HLSLCC before this file is preprocessed, so nothing here includes
// them either.
// ---------------------------------------------------------------------------

// One vertex of the demo meshes: position, normal, vertex color and texture
// coordinate. The locations match Assembly.Rendering.DemoVertex.
struct PassVertexInput
{
    VSP_VK_LOCATION(0) float3 Position : POSITION;
    VSP_VK_LOCATION(1) float3 Normal   : NORMAL;
    VSP_VK_LOCATION(2) float4 Color    : COLOR;
    VSP_VK_LOCATION(3) float2 Uv       : TEXCOORD0;
};

// What the vertex stage hands to the rasterizer. SV_Position is the built-in
// clip-space position; the others are interpolated to the fragment stage.
struct PassVertexOutput
{
    float4 Position      : SV_Position;
    VSP_VK_LOCATION(0) float3 WorldPosition : TEXCOORD0;
    VSP_VK_LOCATION(1) float3 WorldNormal   : TEXCOORD1;
    VSP_VK_LOCATION(2) float4 Color         : TEXCOORD2;
    VSP_VK_LOCATION(3) float2 Uv            : TEXCOORD3;
};

// The fragment stage reads the interpolated values; SV_Position is not needed.
struct PassFragmentInput
{
    VSP_VK_LOCATION(0) float3 WorldPosition : TEXCOORD0;
    VSP_VK_LOCATION(1) float3 WorldNormal   : TEXCOORD1;
    VSP_VK_LOCATION(2) float4 Color         : TEXCOORD2;
    VSP_VK_LOCATION(3) float2 Uv            : TEXCOORD3;
};

// Per-draw constants. The explicit offsets keep the block byte-identical to the
// managed Assembly.Rendering.DemoPushConstants struct, so the pipeline can write
// it straight into the command buffer.
//
// The block follows the Vulkan block layout rules: the matrix and the float4s
// come first so every member sits on its natural alignment, and the scalars
// share the last sixteen bytes.
//
// WorldMatrix is row_major because the managed side uploads a System.Numerics
// Matrix4x4, which stores its rows the way HLSL's row_major layout expects.
struct PassPushConstants
{
    row_major float4x4 WorldMatrix;      // offset 0   (local -> world for this draw)
    VSP_VK_OFFSET(64)  float4 BaseColor;         // rgb = flat color, a = unused
    VSP_VK_OFFSET(80)  float4 TintColor;         // the material's _Tint property
    VSP_VK_OFFSET(96)  float4 LightDirection;    // xyz = direction the light travels, w = intensity
    VSP_VK_OFFSET(112) int    ColorMode;         // 0 red, 1 blue, 2 green, 3 vertex colors
    VSP_VK_OFFSET(116) uint   TextureIndex;      // bindless slot this draw samples
    VSP_VK_OFFSET(120) float  Roughness;         // 0 = mirror, 1 = fully diffuse
    VSP_VK_OFFSET(124) float  SpecularStrength;  // 0 = matte (the 2D content), 1 = shiny
};
[[vk::push_constant]] ConstantBuffer<PassPushConstants> PushConstants;

// Per-frame camera. The ENGINE fills it from the camera the render pipeline
// handed to the frame: its view-projection, its view, its projection, where it
// is and the lens it was configured with. A shader declares the members it
// reads; the ones it does not mention cost nothing.
//
// The matrices are column major, which is what the engine writes, so a world
// position becomes clip space with mul(matrix, vector).
cbuffer CameraUniformBuffer
{
    column_major float4x4 ViewProjectionMatrix;
    column_major float4x4 ViewMatrix;
    column_major float4x4 ProjectionMatrix;
    column_major float4x4 CameraToWorldMatrix;
    float4 CameraPosition;    // xyz = world position, w = aperture
    float4 CameraLens;        // x = focus distance, y = near, z = far
};

// The builtin library. <Vsp/Texture.hlsl> declares the engine's sampled-image
// array and its sampler - without saying where they sit, because the engine
// decides that - and wraps them in the sampling functions every shader shares;
// <Vsp/Transform.hlsl> brings the vector helpers (VspSafeNormalize,
// VspViewDirection, ...) and, through <Vsp/Common.hlsl>, the constants and the
// color functions. <Vsp/Builtin.hlsl> would bring all of it at once.
#include <Vsp/Texture.hlsl>
#include <Vsp/Transform.hlsl>
