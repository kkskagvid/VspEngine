// ---------------------------------------------------------------------------
// Vsp/Common.hlsl - constants and small math helpers every shader may use.
//
// Part of the HLSL builtin library HLSLCC ships. Include it with
//     #include <Vsp/Common.hlsl>
// or take the whole library with <Vsp/Builtin.hlsl>.
//
// The library declares no descriptor binding of its own: the ENGINE decides
// where a resource lives (see VspCore/Graphics/ShaderBindings.h), so a shader
// only names the resources it reads.
// ---------------------------------------------------------------------------

#ifndef VSP_COMMON_INCLUDED
#define VSP_COMMON_INCLUDED

// -------- Constants --------
#define VSP_PI              3.14159265358979323846f
#define VSP_TWO_PI          6.28318530717958647692f
#define VSP_HALF_PI         1.57079632679489661923f
#define VSP_INV_PI          0.31830988618379067154f
#define VSP_DEGREES_TO_RADIANS 0.01745329251994329577f
#define VSP_RADIANS_TO_DEGREES 57.29577951308232087680f

// Smallest value a normalized vector may have on any axis, used to keep a
// division or a reflection from collapsing.
#define VSP_EPSILON         1e-5f

// -------- Scalars --------

// Clamps a value to [0, 1], the range a color channel lives in.
float VspSaturate(float fValue)
{
    return saturate(fValue);
}

// Remaps a value from one range to another without clamping it.
float VspRemap(float fValue, float fInputMin, float fInputMax, float fOutputMin, float fOutputMax)
{
    float fInputRange = fInputMax - fInputMin;
    float fSafeRange = (abs(fInputRange) > VSP_EPSILON) ? fInputRange : VSP_EPSILON;
    return fOutputMin + ((fValue - fInputMin) / fSafeRange) * (fOutputMax - fOutputMin);
}

// Smooth Hermite interpolation, the usual falloff between two edges.
float VspSmoothStep(float fEdge0, float fEdge1, float fValue)
{
    float fRange = fEdge1 - fEdge0;
    float fSafeRange = (abs(fRange) > VSP_EPSILON) ? fRange : VSP_EPSILON;
    float fT = saturate((fValue - fEdge0) / fSafeRange);
    return fT * fT * (3.0f - (2.0f * fT));
}

// Fifth power, the cheap form of the Schlick Fresnel term.
float VspPow5(float fValue)
{
    float fSquared = fValue * fValue;
    return fSquared * fSquared * fValue;
}

// -------- Vectors --------

// Normalizes a vector, returning fFallback for a vector too short to have a
// direction (which is what a degenerate triangle produces).
float3 VspSafeNormalize(float3 fValue, float3 fFallback)
{
    float fLength = length(fValue);
    return (fLength > VSP_EPSILON) ? (fValue / fLength) : fFallback;
}

// Normalizes a vector, falling back to the surface normal.
float3 VspSafeNormalize(float3 fValue)
{
    return VspSafeNormalize(fValue, float3(0.0f, 0.0f, 1.0f));
}

// True when the vector is long enough to have a direction.
bool VspHasDirection(float3 fValue)
{
    return dot(fValue, fValue) > (VSP_EPSILON * VSP_EPSILON);
}

// Reflected direction of an incoming vector around a normal.
float3 VspReflect(float3 fIncident, float3 fNormal)
{
    return reflect(fIncident, fNormal);
}

// Refracted direction; returns the incident direction on total reflection.
float3 VspRefract(float3 fIncident, float3 fNormal, float fIndexOfRefraction)
{
    return refract(fIncident, fNormal, fIndexOfRefraction);
}

// -------- Color --------

// Perceived brightness of a linear color (Rec. 709 weights).
float VspLuminance(float3 fLinearColor)
{
    return dot(fLinearColor, float3(0.2126f, 0.7152f, 0.0722f));
}

// One sRGB channel to linear (the exact curve, not a 2.2 power).
float VspSrgbChannelToLinear(float fChannel)
{
    return (fChannel <= 0.04045f)
        ? (fChannel / 12.92f)
        : pow((fChannel + 0.055f) / 1.055f, 2.4f);
}

// One linear channel to sRGB.
float VspLinearChannelToSrgb(float fChannel)
{
    return (fChannel <= 0.0031308f)
        ? (fChannel * 12.92f)
        : ((1.055f * pow(fChannel, 1.0f / 2.4f)) - 0.055f);
}

// A whole sRGB color (rgb) to linear, leaving alpha alone.
float4 VspSrgbToLinear(float4 fSrgbColor)
{
    return float4(
        VspSrgbChannelToLinear(fSrgbColor.r),
        VspSrgbChannelToLinear(fSrgbColor.g),
        VspSrgbChannelToLinear(fSrgbColor.b),
        fSrgbColor.a);
}

// A whole linear color to sRGB, leaving alpha alone.
float4 VspLinearToSrgb(float4 fLinearColor)
{
    return float4(
        VspLinearChannelToSrgb(fLinearColor.r),
        VspLinearChannelToSrgb(fLinearColor.g),
        VspLinearChannelToSrgb(fLinearColor.b),
        fLinearColor.a);
}

// The ACES filmic curve: maps an unbounded linear color into [0, 1] while
// keeping bright areas from clipping to flat white.
float3 VspAcesTonemap(float3 fLinearColor)
{
    const float fA = 2.51f;
    const float fB = 0.03f;
    const float fC = 2.43f;
    const float fD = 0.59f;
    const float fE = 0.14f;
    float3 fNumerator = fLinearColor * ((fA * fLinearColor) + fB);
    float3 fDenominator = fLinearColor * ((fC * fLinearColor) + fD) + fE;
    return saturate(fNumerator / fDenominator);
}

#endif // VSP_COMMON_INCLUDED
