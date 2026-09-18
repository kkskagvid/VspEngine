// ---------------------------------------------------------------------------
// Shared declarations of the Triangle2D shader.
//
// The Pass block of Triangle2D.vsf includes this file, so the two entry points
// in the shader file stay readable and the declarations can be shared with
// other passes or shaders.
//
// The Vulkan HLSL namespace (vk::) and the VSP_VK_* attribute shorthands are
// injected by HLSLCC before this file is preprocessed, so nothing here includes
// them either.
// ---------------------------------------------------------------------------

// One vertex of the engine's 2D vertex layout: position, RGBA color, texture
// coordinate. The locations match VspEngine.Rendering.Vertex2D.
struct PassVertexInput
{
    VSP_VK_LOCATION(0) float2 Position : POSITION;
    VSP_VK_LOCATION(1) float4 Color    : COLOR;
    VSP_VK_LOCATION(2) float2 Uv       : TEXCOORD0;
};

// What the vertex stage hands to the rasterizer. SV_Position is the built-in
// clip-space position; the other two are interpolated to the fragment stage.
struct PassVertexOutput
{
    float4 Position : SV_Position;
    VSP_VK_LOCATION(0) float4 Color : COLOR;
    VSP_VK_LOCATION(1) float2 Uv    : TEXCOORD0;
};

// The fragment stage reads the interpolated values; SV_Position is not needed.
struct PassFragmentInput
{
    VSP_VK_LOCATION(0) float4 Color : COLOR;
    VSP_VK_LOCATION(1) float2 Uv    : TEXCOORD0;
};

// Per-draw constants. The explicit offsets keep the block byte-identical to the
// managed VspEngine.Rendering.Pipelines.TrianglePushConstants struct, so the
// pipeline can write it straight into the command buffer.
//
// The block follows the Vulkan block layout rules: the float4s come first so
// every member sits on its natural alignment.
struct PassPushConstants
{
    VSP_VK_OFFSET(0)  float4 OverrideColor;    // the flat color of modes 0..2
    VSP_VK_OFFSET(16) float4 TintColor;        // the material's _Tint property
    VSP_VK_OFFSET(32) float2 PositionOffset;   // where the triangle sits this draw
    VSP_VK_OFFSET(40) int    ColorMode;        // 0 red, 1 blue, 2 green, 3 multicolor
    VSP_VK_OFFSET(44) uint   TextureIndex;     // bindless slot this draw samples
};
[[vk::push_constant]] ConstantBuffer<PassPushConstants> PushConstants;

// Per-frame camera. The renderer fills it with an orthographic projection whose
// aspect ratio matches the swapchain, so world units stay square. The matrix is
// column major, which is what the renderer writes.
[[vk::binding(0, 0)]] cbuffer CameraUniformBuffer
{
    column_major float4x4 ViewProjectionMatrix;
};

// Bindless texture access: the pipeline binds one large sampled-image array and
// a single shared sampler, and every draw picks its slot through the push
// constants.
[[vk::binding(1, 0)]] Texture2D<float4> BindlessTextures[] : register(t0, space0);
[[vk::binding(2, 0)]] SamplerState BindlessSampler : register(s0, space0);
