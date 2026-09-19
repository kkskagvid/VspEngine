// ---------------------------------------------------------------------------
// Vsp/Texture.hlsl - reading the engine's bindless texture array.
//
// The engine binds ONE sampled-image array and ONE sampler (see
// VspCore/Graphics/ShaderBindings.h). Both stages of a pass share them, and a
// shader never writes a binding: HLSLCC numbers what a shader declares to
// match the engine's set.
//
// A shader that declares its own bindless resources with exactly these names
// gets the same binding - the names are the identity of a resource. A shader
// that declares its own differently named array should define
// VSP_NO_BINDLESS_DECLARATIONS before including this file.
// ---------------------------------------------------------------------------

#ifndef VSP_TEXTURE_INCLUDED
#define VSP_TEXTURE_INCLUDED

#include <Vsp/Common.hlsl>

#ifndef VSP_NO_BINDLESS_DECLARATIONS

// The engine's sampled-image array: every texture a draw may read, addressed by
// the slot the engine reported when the texture was created.
Texture2D<float4> BindlessTextures[];

// The one sampler every bindless texture is read with.
SamplerState BindlessSampler;

#endif // VSP_NO_BINDLESS_DECLARATIONS

// Samples a bindless texture at a coordinate. The index has to be marked
// non-uniform: it comes from per-draw data, so neighbouring pixels may read
// different slots and the hardware may not treat the fetch as uniform.
float4 VspSampleTexture(uint uTextureIndex, float2 fUv)
{
    return BindlessTextures[NonUniformResourceIndex(uTextureIndex)].Sample(BindlessSampler, fUv);
}

// Samples a bindless texture at an explicit level of detail.
float4 VspSampleTextureLod(uint uTextureIndex, float2 fUv, float fLevelOfDetail)
{
    return BindlessTextures[NonUniformResourceIndex(uTextureIndex)].SampleLevel(
        BindlessSampler, fUv, fLevelOfDetail);
}

// Samples with explicit derivatives, for a shader that computes its own
// coordinates (a projection, a reflection, a flow map).
float4 VspSampleTextureGradient(uint uTextureIndex, float2 fUv, float2 fDdx, float2 fDdy)
{
    return BindlessTextures[NonUniformResourceIndex(uTextureIndex)].SampleGrad(
        BindlessSampler, fUv, fDdx, fDdy);
}

// Fetches one texel without filtering or wrapping.
float4 VspFetchTexture(uint uTextureIndex, int2 nPixelCoordinate, uint uMipLevel)
{
    return BindlessTextures[NonUniformResourceIndex(uTextureIndex)].Load(
        int3(nPixelCoordinate, (int)uMipLevel));
}

// Size of one mip level of a bindless texture, in texels.
int2 VspGetTextureSize(uint uTextureIndex, uint uMipLevel)
{
    uint uWidth = 0;
    uint uHeight = 0;
    uint uLevels = 0;
    BindlessTextures[NonUniformResourceIndex(uTextureIndex)].GetDimensions(uMipLevel, uWidth, uHeight, uLevels);
    return int2((int)uWidth, (int)uHeight);
}

// Samples a texture that holds a normal map and turns it into a tangent-space
// normal. fNormalScale scales the deviation from straight up (0 = flat).
float3 VspSampleTangentSpaceNormal(uint uTextureIndex, float2 fUv, float fNormalScale = 1.0f)
{
    float4 sampled = VspSampleTexture(uTextureIndex, fUv);
    float3 tangentSpaceNormal = (sampled.xyz * 2.0f) - 1.0f;
    tangentSpaceNormal.xy *= fNormalScale;
    return VspSafeNormalize(tangentSpaceNormal, float3(0.0f, 0.0f, 1.0f));
}

// Reads a roughness value out of a metallic/roughness map (glTF packing: the
// green channel is roughness, the blue channel is metallic).
float VspSampleRoughness(uint uTextureIndex, float2 fUv)
{
    return VspSampleTexture(uTextureIndex, fUv).g;
}

// Reads a metallic value out of a metallic/roughness map.
float VspSampleMetallic(uint uTextureIndex, float2 fUv)
{
    return VspSampleTexture(uTextureIndex, fUv).b;
}

// Reads an occlusion value out of a packed map.
float VspSampleOcclusion(uint uTextureIndex, float2 fUv)
{
    return VspSampleTexture(uTextureIndex, fUv).r;
}

#endif // VSP_TEXTURE_INCLUDED
