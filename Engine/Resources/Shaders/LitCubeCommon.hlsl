// ---------------------------------------------------------------------------
// Shared declarations of the demo's 3D shader.
//
// The Pass block of LitCube.vsf includes this file, so the entry points stay
// readable and the declarations can be shared with another pass or shader.
//
// The demo draws one lit, per-face colored cube. Every draw carries its own
// world matrix, and the light is a single directional source shining STRAIGHT
// DOWN (its travel direction is (0, -1, 0), i.e. 90 degrees from the horizon):
// the top face catches the full light, the four sides catch the ambient term
// only, which is what makes the shape read as a solid and the spin visible.
//
// NOTE: no resource names a binding. The ENGINE decides where every resource
// lives and HLSLCC applies that decision to the compiled SPIR-V, so a shader
// only says WHICH resource it uses, never where it sits.
//
// The Vulkan HLSL namespace (vk::) and the VSP_VK_* attribute shorthands are
// injected by HLSLCC before this file is preprocessed, so nothing here includes
// them either.
// ---------------------------------------------------------------------------

// One vertex of the demo mesh: position, normal and vertex colour. The
// locations match Assembly.Rendering.DemoVertex, field for field - a shader
// declares exactly the attributes it reads, because an attribute the vertex
// stage never consumes is a pipeline/shader mismatch the validation layer
// reports.
struct LitCubeVertexInput
{
    VSP_VK_LOCATION(0) float3 Position : POSITION;
    VSP_VK_LOCATION(1) float3 Normal   : NORMAL;
    VSP_VK_LOCATION(2) float4 Color    : COLOR;
};

// What the vertex stage hands to the rasterizer. SV_Position is the built-in
// clip-space position; the others are interpolated to the fragment stage.
struct LitCubeVertexOutput
{
    float4 Position : SV_Position;
    VSP_VK_LOCATION(0) float3 WorldPosition : TEXCOORD0;
    VSP_VK_LOCATION(1) float3 WorldNormal   : TEXCOORD1;
    VSP_VK_LOCATION(2) float4 Color         : TEXCOORD2;
};

// The fragment stage reads the interpolated values; SV_Position is not needed.
struct LitCubeFragmentInput
{
    VSP_VK_LOCATION(0) float3 WorldPosition : TEXCOORD0;
    VSP_VK_LOCATION(1) float3 WorldNormal   : TEXCOORD1;
    VSP_VK_LOCATION(2) float4 Color         : TEXCOORD2;
};

// Per-draw constants. The explicit offsets keep the block byte-identical to the
// managed Assembly.Rendering.LitCubePushConstants struct, so the pipeline can
// write it straight into the command buffer.
//
// WorldMatrix is row_major because the managed side uploads a
// System.Numerics Matrix4x4, which stores its rows the way HLSL's row_major
// layout expects.
struct LitCubePushConstants
{
    row_major float4x4 WorldMatrix;      // offset 0   (local -> world for this draw)
    VSP_VK_OFFSET(64)  float4 BaseColor;        // offset 64  (the material's colour)
    VSP_VK_OFFSET(80)  float4 LightDirection;   // offset 80  (xyz = travel direction, w = intensity)
    VSP_VK_OFFSET(96)  float4 AmbientColor;     // offset 96  (rgb = ambient fill, a = unused)
};
[[vk::push_constant]] ConstantBuffer<LitCubePushConstants> PushConstants;

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

// The builtin library brings the constants and the small vector helpers
// (VspSaturate, VspSafeNormalize, VspRemap, ...). <Vsp/Builtin.hlsl> would bring
// all of it at once; a lit untextured cube needs no texture helpers at all.
#include <Vsp/Common.hlsl>
