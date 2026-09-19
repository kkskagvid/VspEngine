// ---------------------------------------------------------------------------
// Vsp/Normal.hlsl - normal maps and tangent frames.
//
// Two ways to get a world-space normal out of a normal map: from a tangent
// frame the mesh carries, or from the derivatives of the position - which is
// what a mesh without tangents needs and what VspBuildTangentFrame does.
// ---------------------------------------------------------------------------

#ifndef VSP_NORMAL_INCLUDED
#define VSP_NORMAL_INCLUDED

#include <Vsp/Common.hlsl>

// Turns a sampled normal map into a tangent-space normal: the stored color is
// [0, 1] per channel, the normal is [-1, 1].
float3 VspUnpackNormalMap(float4 fSampledNormal, float fNormalScale = 1.0f)
{
    float3 tangentSpaceNormal = (fSampledNormal.xyz * 2.0f) - 1.0f;
    tangentSpaceNormal.xy *= fNormalScale;
    return VspSafeNormalize(tangentSpaceNormal, float3(0.0f, 0.0f, 1.0f));
}

// Builds the tangent frame of a surface from the derivatives of its world
// position and texture coordinate - the frame a normal map needs when the mesh
// carries no tangents of its own.
//
// The derivatives are the ones the fragment stage computes anyway:
//     float3 positionDdx = ddx(input.WorldPosition);
//     float3 positionDdy = ddy(input.WorldPosition);
//     float2 uvDdx = ddx(input.Uv);
//     float2 uvDdy = ddy(input.Uv);
void VspBuildTangentFrame(
    float3 fNormal,
    float3 fPositionDdx,
    float3 fPositionDdy,
    float2 fUvDdx,
    float2 fUvDdy,
    out float3 outTangent,
    out float3 outBitangent)
{
    // The surface's gradient along the two texture axes.
    float fDeterminant = (fUvDdx.x * fUvDdy.y) - (fUvDdy.x * fUvDdx.y);
    if (abs(fDeterminant) < VSP_EPSILON)
    {
        // A degenerate or untextured surface: any frame perpendicular to the
        // normal works, so pick one that is stable for the normal at hand.
        float3 helper = (abs(fNormal.z) < 0.999f) ? float3(0.0f, 0.0f, 1.0f) : float3(1.0f, 0.0f, 0.0f);
        outTangent = VspSafeNormalize(cross(helper, fNormal));
        outBitangent = cross(fNormal, outTangent);
        return;
    }

    float fInverseDeterminant = 1.0f / fDeterminant;
    float3 tangent = ((fPositionDdx * fUvDdy.y) - (fPositionDdy * fUvDdx.y)) * fInverseDeterminant;
    float3 bitangent = ((fPositionDdy * fUvDdx.x) - (fPositionDdx * fUvDdy.x)) * fInverseDeterminant;

    // Gram-Schmidt: the tangent is made perpendicular to the normal, and the
    // bitangent keeps the handedness the normal map was authored with.
    outTangent = VspSafeNormalize(tangent - (fNormal * dot(fNormal, tangent)), float3(1.0f, 0.0f, 0.0f));
    outBitangent = VspSafeNormalize(cross(fNormal, outTangent), cross(fNormal, outTangent));
    if (dot(outBitangent, bitangent) < 0.0f)
    {
        outBitangent = -outBitangent;
    }
}

// The same frame, for a shader that wants the two vectors as one value.
float3x3 VspBuildTangentFrameMatrix(
    float3 fNormal,
    float3 fPositionDdx,
    float3 fPositionDdy,
    float2 fUvDdx,
    float2 fUvDdy)
{
    float3 tangent, bitangent;
    VspBuildTangentFrame(fNormal, fPositionDdx, fPositionDdy, fUvDdx, fUvDdy, tangent, bitangent);
    return float3x3(tangent, bitangent, fNormal);
}

// A tangent-space normal turned into a world-space one.
float3 VspTangentSpaceToWorldNormal(float3 fTangentSpaceNormal, float3 fNormal, float3 fTangent, float3 fBitangent)
{
    float3 worldNormal = (fTangent * fTangentSpaceNormal.x) +
        (fBitangent * fTangentSpaceNormal.y) +
        (fNormal * fTangentSpaceNormal.z);
    return VspSafeNormalize(worldNormal, fNormal);
}

// A tangent-space normal turned into a world-space one by the frame a shader
// built with VspBuildTangentFrameMatrix.
float3 VspTransformNormalToWorld(float3 fTangentSpaceNormal, float3x3 fTangentFrame)
{
    return VspSafeNormalize(mul(fTangentSpaceNormal, fTangentFrame), float3(0.0f, 0.0f, 1.0f));
}

// Blends two world-space normals. The result leans towards fSecondNormal by
// fBlend and stays a unit vector, which is what adding detail layers needs.
float3 VspBlendNormals(float3 fFirstNormal, float3 fSecondNormal, float fBlend)
{
    float3 blended = lerp(fFirstNormal, float3(fSecondNormal.xy, fFirstNormal.z), fBlend);
    return VspSafeNormalize(blended, fFirstNormal);
}

// Flips a normal to face the viewer, which a double-sided surface needs: a
// back face shows the other side of the same geometry.
float3 VspFaceForward(float3 fNormal, float3 fViewDirection)
{
    return (dot(fNormal, fViewDirection) < 0.0f) ? -fNormal : fNormal;
}

#endif // VSP_NORMAL_INCLUDED
