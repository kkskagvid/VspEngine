// ---------------------------------------------------------------------------
// Vsp/Transform.hlsl - building transform matrices and moving vectors with
// them.
//
// The engine's camera block is the game's (a uniform buffer always lands on
// the engine's uniform-buffer binding), so this file never declares one: pass
// the matrix in.
// ---------------------------------------------------------------------------

#ifndef VSP_TRANSFORM_INCLUDED
#define VSP_TRANSFORM_INCLUDED

#include <Vsp/Common.hlsl>

// Rotation matrix of an Euler angle triple in radians, applied in Z, then Y,
// then X - the order an object imported with a Z-up convention expects.
float3x3 VspRotationMatrixFromEuler(float3 fEulerRadians)
{
    float fSinX, fCosX, fSinY, fCosY, fSinZ, fCosZ;
    sincos(fEulerRadians.x, fSinX, fCosX);
    sincos(fEulerRadians.y, fSinY, fCosY);
    sincos(fEulerRadians.z, fSinZ, fCosZ);

    float3x3 rotation;
    rotation[0] = float3(fCosY * fCosZ, fCosX * fSinZ + (fSinX * fSinY * fCosZ), (fSinX * fSinZ) - (fCosX * fSinY * fCosZ));
    rotation[1] = float3(-fCosY * fSinZ, (fCosX * fCosZ) - (fSinX * fSinY * fSinZ), (fSinX * fCosZ) + (fCosX * fSinY * fSinZ));
    rotation[2] = float3(fSinY, -fSinX * fCosY, fCosX * fCosY);
    return rotation;
}

// A full scale/rotation/translation matrix, in the order the engine applies
// them: scale first, then rotate, then translate.
float4x4 VspComposeTrs(float3 fTranslation, float3 fEulerRadians, float3 fScale)
{
    float3x3 rotation = VspRotationMatrixFromEuler(fEulerRadians);

    // The scale goes into the axes of the rotation, which is the same as
    // rotating a scaled basis.
    float3x3 scaledRotation;
    scaledRotation[0] = rotation[0] * fScale.x;
    scaledRotation[1] = rotation[1] * fScale.y;
    scaledRotation[2] = rotation[2] * fScale.z;

    float4x4 transform = float4x4(
        float4(scaledRotation[0], 0.0f),
        float4(scaledRotation[1], 0.0f),
        float4(scaledRotation[2], 0.0f),
        float4(fTranslation, 1.0f));
    return transform;
}

// A scale/rotation/translation matrix from a single float4x4 the game stored,
// for shaders that already carry one.
float4x4 VspComposeTranslation(float3 fTranslation)
{
    return float4x4(
        float4(1.0f, 0.0f, 0.0f, 0.0f),
        float4(0.0f, 1.0f, 0.0f, 0.0f),
        float4(0.0f, 0.0f, 1.0f, 0.0f),
        float4(fTranslation, 1.0f));
}

// Moves a position: the translation column applies (w = 1).
float3 VspTransformPoint(float4x4 fTransform, float3 fPosition)
{
    return mul(fTransform, float4(fPosition, 1.0f)).xyz;
}

// Moves a direction: no translation (w = 0).
float3 VspTransformDirection(float4x4 fTransform, float3 fDirection)
{
    return mul(fTransform, float4(fDirection, 0.0f)).xyz;
}

// Moves a normal. A normal is not a direction of the surface but of its plane,
// so a non-uniform scale needs the inverse transpose of the transform - pass
// that one in when the scale is not uniform.
float3 VspTransformNormal(float4x4 fInverseTransposeTransform, float3 fNormal)
{
    return VspSafeNormalize(mul(fNormal, (float3x3)fInverseTransposeTransform));
}

// Inverse of a 3x3 matrix (the adjugate over the determinant). Written out
// because the HLSL intrinsic inverse() is not available on every compiler.
float3x3 VspInverse3x3(float3x3 fTransform)
{
    float3 row0 = fTransform[0];
    float3 row1 = fTransform[1];
    float3 row2 = fTransform[2];

    float3 cofactorRow0 = float3(
        (row1.y * row2.z) - (row1.z * row2.y),
        (row1.z * row2.x) - (row1.x * row2.z),
        (row1.x * row2.y) - (row1.y * row2.x));
    float3 cofactorRow1 = float3(
        (row2.y * row0.z) - (row2.z * row0.y),
        (row2.z * row0.x) - (row2.x * row0.z),
        (row2.x * row0.y) - (row2.y * row0.x));
    float3 cofactorRow2 = float3(
        (row0.y * row1.z) - (row0.z * row1.y),
        (row0.z * row1.x) - (row0.x * row1.z),
        (row0.x * row1.y) - (row0.y * row1.x));

    float fDeterminant = dot(row0, cofactorRow0);
    float fSafeDeterminant = (abs(fDeterminant) > VSP_EPSILON) ? fDeterminant : VSP_EPSILON;

    // The inverse is the transposed cofactor matrix over the determinant, which
    // is the adjugate written out by columns.
    float3x3 inverseTransform;
    inverseTransform[0] = float3(cofactorRow0.x, cofactorRow1.x, cofactorRow2.x) / fSafeDeterminant;
    inverseTransform[1] = float3(cofactorRow0.y, cofactorRow1.y, cofactorRow2.y) / fSafeDeterminant;
    inverseTransform[2] = float3(cofactorRow0.z, cofactorRow1.z, cofactorRow2.z) / fSafeDeterminant;
    return inverseTransform;
}

// Inverse transpose of the upper-left 3x3, which is what normals travel with.
float3x3 VspInverseTranspose(float3x3 fTransform)
{
    return transpose(VspInverse3x3(fTransform));
}

// View direction of a camera at fCameraPosition looking at fWorldPosition.
float3 VspViewDirection(float3 fWorldPosition, float3 fCameraPosition)
{
    return VspSafeNormalize(fCameraPosition - fWorldPosition, float3(0.0f, 0.0f, 1.0f));
}

#endif // VSP_TRANSFORM_INCLUDED
