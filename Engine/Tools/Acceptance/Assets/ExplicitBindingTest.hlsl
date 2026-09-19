// ---------------------------------------------------------------------------
// ExplicitBindingTest.hlsl
//
// The resources of a pass that numbers them itself. The camera block is put at
// binding 7 - a number the engine never assigns - so a compiled shader that
// reports 7 is one HLSLCC left alone.
// ---------------------------------------------------------------------------

[[vk::binding(7, 0)]] cbuffer ExplicitCameraBlock
{
    column_major float4x4 ViewProjectionMatrix;
};

struct ExplicitVertexInput
{
    [[vk::location(0)]] float3 Position : POSITION;
    [[vk::location(1)]] float4 Color : COLOR0;
};

struct ExplicitFragmentInput
{
    float4 Position : SV_POSITION;
    float4 Color : COLOR0;
};
