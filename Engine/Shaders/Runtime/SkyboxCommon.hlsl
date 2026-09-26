// ---------------------------------------------------------------------------
// SkyboxCommon.hlsl - what Engine/Shaders/Runtime/Skybox.vsf declares.
//
// The sky needs NO geometry. A box, a sphere or a cube map would all be ways of
// answering one question - "which direction does this pixel look along?" - so
// this shader asks it directly: it draws ONE fullscreen triangle (three
// vertices, no vertex buffer, no index buffer) and every fragment reconstructs
// the world direction of the ray through its pixel from the ENGINE's camera
// block. The colour is then a pure function of that direction, which is what
// makes a sky a sky: it never moves with the camera, only with where the camera
// LOOKS.
//
//   vertex    builds the three clip-space corners of the fullscreen triangle
//             from SV_VertexID and passes the clip position on;
//   fragment  turns that clip position back into a view direction with the
//             camera's projection matrix, rotates it into the world with the
//             camera-to-world matrix, and shades the gradient and the sun.
//
// The reconstruction reads the camera the RENDER PIPELINE handed to the frame
// (CommandBuffer.SetCamera), so the sky can never disagree with the scene about
// where the camera is or which way it looks.
//
// NOTE: no resource names a binding. The ENGINE decides where every resource
// lives and HLSLCC applies that decision to the compiled SPIR-V, so a shader
// only says WHICH resource it uses, never where it sits.
//
// The Vulkan HLSL namespace (vk::) and the VSP_VK_* attribute shorthands are
// injected by HLSLCC before this file is preprocessed, so nothing here includes
// them either.
// ---------------------------------------------------------------------------

// What the vertex stage hands to the rasterizer. SV_Position is the built-in
// clip-space position; the clip position travels on as an interpolated value,
// because only the FRAGMENT stage knows which pixel it is shading.
//
// A fullscreen triangle has no vertex inputs at all: the three corners come
// from the vertex index, which is why the pipeline declares no vertex
// attributes and the draw binds no vertex buffer.
struct SkyboxVertexOutput
{
    float4 Position     : SV_Position;
    VSP_VK_LOCATION(0) float2 ClipPosition : TEXCOORD0;
};

// The fragment stage reads the interpolated clip position; SV_Position is not
// needed, because the interpolated value is already the position between the
// triangle's corners.
struct SkyboxFragmentInput
{
    VSP_VK_LOCATION(0) float2 ClipPosition : TEXCOORD0;
};

// Per-draw constants. The explicit offsets keep the block byte-identical to the
// managed Assembly.Rendering.SkyboxPushConstants struct, so the pipeline can
// write it straight into the command buffer.
//
// Every colour here is an sRGB value the shader writes straight into the
// back buffer, exactly like the demo's other shaders: the swapchain image is a
// non-linear UNORM format, so nothing converts between the two on the way out.
struct SkyboxPushConstants
{
    float4 ZenithColor;    // offset 0   rgb = the colour overhead, a = unused
    float4 HorizonColor;   // offset 16  rgb = the colour at eye level, a = unused
    float4 GroundColor;    // offset 32  rgb = the colour below the horizon, a = unused
    float4 SunDirection;   // offset 48  xyz = the direction TOWARDS the sun, w = intensity
    float4 SunColor;       // offset 64  rgb = the sun's colour, a = cosine of its angular radius
    float4 SkyShape;       // offset 80  x = zenith falloff, y = below-horizon falloff, z = glow falloff
};
[[vk::push_constant]] ConstantBuffer<SkyboxPushConstants> PushConstants;

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
// (VspSaturate, VspSafeNormalize, VspRemap, ...).
#include <Vsp/Common.hlsl>
