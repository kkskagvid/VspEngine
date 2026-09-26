// ---------------------------------------------------------------------------
// Shared declarations of the demo's interface shader.
//
// The UI is drawn the same way everything else is - a mesh through the same
// camera, culled and depth-tested in the same pass - but its vertices are
// already in CLIP SPACE: the managed layout transforms every pixel-space corner
// with the engine's NATIVE pixel-space projection on the CPU, so this shader
// has no matrix to multiply and the whole interface is one buffer upload.
//
// The fragment stage multiplies the vertex colour by the sampled texture's
// ALPHA. That single convention covers everything a flat interface draws:
//   * a solid panel samples the engine's 1x1 white texture, whose alpha is 1,
//     so the colour comes through untouched;
//   * a glyph samples the font's coverage atlas, which stores the glyph
//     coverage in the alpha channel and white in the colour channels, so the
//     glyph takes the colour it was asked to draw with.
//
// NOTE: no resource names a binding - the ENGINE decides where every resource
// lives and HLSLCC applies that decision to the compiled SPIR-V.
// ---------------------------------------------------------------------------

// One UI vertex; the layout matches Assembly.UI (VspEngine.UI.UiVertex).
struct UiVertexInput
{
    VSP_VK_LOCATION(0) float2 Position : POSITION;    // already in clip space
    VSP_VK_LOCATION(1) float2 Uv       : TEXCOORD0;
    VSP_VK_LOCATION(2) float4 Color    : COLOR;
};

struct UiVertexOutput
{
    float4 Position : SV_Position;
    VSP_VK_LOCATION(0) float2 Uv    : TEXCOORD0;
    VSP_VK_LOCATION(1) float4 Color : COLOR;
};

struct UiFragmentInput
{
    VSP_VK_LOCATION(0) float2 Uv    : TEXCOORD0;
    VSP_VK_LOCATION(1) float4 Color : COLOR;
};

// Per-draw constants; the block is byte-identical to the managed
// VspEngine.UI.UiPushConstants struct.
struct UiPushConstants
{
    float4 TintColor;                        // offset 0   (multiplies the vertex colour)
    VSP_VK_OFFSET(16) uint TextureIndex;     // offset 16  (bindless slot this draw samples)
    VSP_VK_OFFSET(20) float3 UnusedPadding;  // offset 20  (keeps the block 32 bytes)
};
[[vk::push_constant]] ConstantBuffer<UiPushConstants> PushConstants;

// The bindless array and its sampling helpers.
#include <Vsp/Texture.hlsl>
