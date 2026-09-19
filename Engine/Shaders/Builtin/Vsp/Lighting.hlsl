// ---------------------------------------------------------------------------
// Vsp/Lighting.hlsl - surfaces, lights and the shading models a shader picks
// from: Lambert, Blinn-Phong and a metallic/roughness GGX model.
//
// A light is a plain value struct, so a shader can hold lights in a constant
// buffer, in a structured buffer or in local variables:
//
//     VspLight lights[VSP_MAX_LIGHTS];
//     float3 color = VspShadeLights(surface, lights, uLightCount, fAmbient);
//
// The struct keeps every member a float4 so its layout is the same in a
// constant buffer as in registers.
// ---------------------------------------------------------------------------

#ifndef VSP_LIGHTING_INCLUDED
#define VSP_LIGHTING_INCLUDED

#include <Vsp/Common.hlsl>

// Number of lights VspShadeLights takes at most. A shader that passes fewer
// reports the count it filled in.
#ifndef VSP_MAX_LIGHTS
#define VSP_MAX_LIGHTS 8
#endif

// What a light does, stored in VspLight::fDirectionAndType.w.
#define VSP_LIGHT_TYPE_DIRECTIONAL 0.0f
#define VSP_LIGHT_TYPE_POINT       1.0f
#define VSP_LIGHT_TYPE_SPOT        2.0f

// One light. Every member is a float4, so the same value can live in a
// constant buffer, a structured buffer or a local array without repacking.
struct VspLight
{
    float4 fDirectionAndType;    // xyz = direction the light travels FROM the light, w = type
    float4 fPositionAndRange;    // xyz = world position (point and spot), w = range in world units
    float4 fColorAndIntensity;   // rgb = linear color, w = intensity
    float4 fSpotAngles;          // x = cos(inner angle), y = cos(outer angle)
};

// -------- Reading a light --------

float VspGetLightType(VspLight light)
{
    return light.fDirectionAndType.w;
}

// Direction from the light towards the scene; a directional light keeps it in
// fDirectionAndType.xyz.
float3 VspGetLightDirection(VspLight light)
{
    return VspSafeNormalize(light.fDirectionAndType.xyz, float3(0.0f, -1.0f, 0.0f));
}

float3 VspGetLightPosition(VspLight light)
{
    return light.fPositionAndRange.xyz;
}

float VspGetLightRange(VspLight light)
{
    return light.fPositionAndRange.w;
}

// Linear color of the light, already multiplied by its intensity.
float3 VspGetLightRadiance(VspLight light)
{
    return light.fColorAndIntensity.rgb * light.fColorAndIntensity.w;
}

// Builds a directional light: fDirection points from the light into the scene.
VspLight VspMakeDirectionalLight(float3 fDirection, float3 fColor, float fIntensity)
{
    VspLight light;
    light.fDirectionAndType = float4(normalize(fDirection), VSP_LIGHT_TYPE_DIRECTIONAL);
    light.fPositionAndRange = float4(0.0f, 0.0f, 0.0f, 0.0f);
    light.fColorAndIntensity = float4(fColor, fIntensity);
    light.fSpotAngles = float4(-1.0f, -1.0f, 0.0f, 0.0f);
    return light;
}

// Builds a point light. fRange is the distance at which it has faded out.
VspLight VspMakePointLight(float3 fPosition, float fRange, float3 fColor, float fIntensity)
{
    VspLight light;
    light.fDirectionAndType = float4(0.0f, -1.0f, 0.0f, VSP_LIGHT_TYPE_POINT);
    light.fPositionAndRange = float4(fPosition, fRange);
    light.fColorAndIntensity = float4(fColor, fIntensity);
    light.fSpotAngles = float4(-1.0f, -1.0f, 0.0f, 0.0f);
    return light;
}

// Builds a spot light. fDirection points from the light into the scene; the
// two angles are the full cone angles in radians.
VspLight VspMakeSpotLight(
    float3 fPosition, float3 fDirection, float fRange,
    float fInnerAngleRadians, float fOuterAngleRadians,
    float3 fColor, float fIntensity)
{
    VspLight light;
    light.fDirectionAndType = float4(normalize(fDirection), VSP_LIGHT_TYPE_SPOT);
    light.fPositionAndRange = float4(fPosition, fRange);
    light.fColorAndIntensity = float4(fColor, fIntensity);
    light.fSpotAngles = float4(
        cos(fInnerAngleRadians * 0.5f), cos(fOuterAngleRadians * 0.5f), 0.0f, 0.0f);
    return light;
}

// -------- Surface --------

// What is shaded: where it faces, which way the viewer looks at it and what it
// is made of.
struct VspSurface
{
    float3 fNormal;         // world-space unit normal
    float3 fViewDirection;  // world-space unit vector from the surface to the camera
    float3 fAlbedo;         // linear base color
    float3 fSpecularColor;  // linear specular color for the non-metal models
    float fMetallic;        // 0 = dielectric, 1 = metal
    float fRoughness;       // 0 = mirror, 1 = fully diffuse
    float fShininess;       // Blinn-Phong exponent used by VspShadeBlinnPhong
};

// A surface with sensible defaults for a non-metal, mid-rough material.
VspSurface VspMakeSurface(float3 fNormal, float3 fViewDirection, float3 fAlbedo)
{
    VspSurface surface;
    surface.fNormal = VspSafeNormalize(fNormal, float3(0.0f, 0.0f, 1.0f));
    surface.fViewDirection = VspSafeNormalize(fViewDirection, float3(0.0f, 0.0f, 1.0f));
    surface.fAlbedo = fAlbedo;
    surface.fSpecularColor = 0.04f;
    surface.fMetallic = 0.0f;
    surface.fRoughness = 0.5f;
    surface.fShininess = 32.0f;
    return surface;
}

// -------- Attenuation --------

// How much of a point light reaches a surface at the given distance. The
// smooth window keeps a light from ending in a visible ring at its range.
float VspGetDistanceAttenuation(float fDistance, float fRange)
{
    if (fRange <= VSP_EPSILON)
    {
        return 1.0f;   // A light without a range reaches everything.
    }

    float fNormalizedDistance = fDistance / fRange;
    float fWindow = saturate(1.0f - (fNormalizedDistance * fNormalizedDistance * fNormalizedDistance * fNormalizedDistance));
    float fFalloff = 1.0f / max(fDistance * fDistance, VSP_EPSILON);
    return fWindow * fWindow * fFalloff;
}

// How much of a spot light reaches a direction that is fCosAngle away from the
// cone axis. The two cosines come from fSpotAngles.
float VspGetSpotAttenuation(float fCosAngle, float fInnerCosAngle, float fOuterCosAngle)
{
    return saturate((fCosAngle - fOuterCosAngle) / max(fInnerCosAngle - fOuterCosAngle, VSP_EPSILON));
}

// -------- Distribution and geometry terms --------

// Trowbridge-Reitz (GGX) distribution: how much of the microsurface faces the
// half vector.
float VspGetGgxDistribution(float fNormalDotHalf, float fRoughness)
{
    float fAlpha = max(fRoughness * fRoughness, VSP_EPSILON);
    float fAlphaSquared = fAlpha * fAlpha;
    float fDenominator = (fNormalDotHalf * fNormalDotHalf * (fAlphaSquared - 1.0f)) + 1.0f;
    return fAlphaSquared / max(VSP_PI * fDenominator * fDenominator, VSP_EPSILON);
}

// Smith's geometry term with the Schlick-GGX approximation: how much of the
// microsurface is visible from one direction.
float VspGetGgxGeometry(float fNormalDotDirection, float fRoughness)
{
    float fK = (fRoughness + 1.0f) * (fRoughness + 1.0f) / 8.0f;
    return fNormalDotDirection / max(fNormalDotDirection * (1.0f - fK) + fK, VSP_EPSILON);
}

// Fresnel reflectance: how much light reflects instead of entering the
// surface.
float3 VspGetFresnel(float fCosTheta, float3 fReflectanceAtNormalIncidence)
{
    return fReflectanceAtNormalIncidence +
        ((1.0f - fReflectanceAtNormalIncidence) * VspPow5(saturate(1.0f - fCosTheta)));
}

// -------- Shading models --------

// Diffuse term of the Lambert model: light times the angle to the surface.
float3 VspShadeLambert(VspSurface surface, float3 fLightDirection, float3 fLightRadiance)
{
    float fNormalDotLight = saturate(dot(surface.fNormal, fLightDirection));
    return surface.fAlbedo * fLightRadiance * fNormalDotLight * VSP_INV_PI;
}

// Blinn-Phong: Lambert plus a highlight around the half vector. fShininess is
// the material's exponent - the higher it is, the tighter the highlight.
float3 VspShadeBlinnPhong(
    VspSurface surface,
    float3 fLightDirection,
    float3 fLightRadiance,
    float3 fSpecularColor)
{
    float fNormalDotLight = saturate(dot(surface.fNormal, fLightDirection));
    float3 diffuse = surface.fAlbedo * fLightRadiance * fNormalDotLight * VSP_INV_PI;

    float3 halfVector = VspSafeNormalize(fLightDirection + surface.fViewDirection, surface.fNormal);
    float fNormalDotHalf = saturate(dot(surface.fNormal, halfVector));
    float fSpecular = pow(fNormalDotHalf, max(surface.fShininess, 1.0f));

    // The highlight only exists where the surface faces the light, and is
    // masked by the angle to the viewer so grazing highlights fade out.
    float fNormalDotView = saturate(dot(surface.fNormal, surface.fViewDirection));
    float fVisibility = (fNormalDotLight > 0.0f) ? 1.0f : 0.0f;
    return diffuse + (fSpecularColor * fSpecular * fVisibility * fLightRadiance * fNormalDotView);
}

// Metallic/roughness GGX: the model glTF and most modern pipelines use.
float3 VspShadeMetallicRoughness(VspSurface surface, float3 fLightDirection, float3 fLightRadiance)
{
    float3 halfVector = VspSafeNormalize(fLightDirection + surface.fViewDirection, surface.fNormal);

    float fNormalDotLight = saturate(dot(surface.fNormal, fLightDirection));
    float fNormalDotView = saturate(dot(surface.fNormal, surface.fViewDirection));
    float fNormalDotHalf = saturate(dot(surface.fNormal, halfVector));
    float fViewDotHalf = saturate(dot(surface.fViewDirection, halfVector));

    // A metal has no diffuse color and tints its reflections with the albedo.
    float fRoughness = clamp(surface.fRoughness, 0.04f, 1.0f);
    float3 fDiffuseColor = surface.fAlbedo * (1.0f - surface.fMetallic);
    float3 fSpecularColor = lerp(surface.fSpecularColor, surface.fAlbedo, surface.fMetallic);

    float fDistribution = VspGetGgxDistribution(fNormalDotHalf, fRoughness);
    float fGeometry = VspGetGgxGeometry(fNormalDotView, fRoughness) *
        VspGetGgxGeometry(fNormalDotLight, fRoughness);
    float3 fFresnel = VspGetFresnel(fViewDotHalf, fSpecularColor);

    float fSpecularDenominator = 4.0f * max(fNormalDotView * fNormalDotLight, VSP_EPSILON);
    float3 specular = (fDistribution * fGeometry * fFresnel) / fSpecularDenominator;
    float3 diffuse = fDiffuseColor * VSP_INV_PI;

    return (diffuse + specular) * fLightRadiance * fNormalDotLight;
}

// -------- Putting a light and a surface together --------

// Direction from the surface towards the light plus how much of it arrives,
// for whichever light is passed in.
float3 VspGetLightDirectionToSurface(VspLight light, float3 fWorldPosition, out float outAttenuation)
{
    float fLightType = VspGetLightType(light);

    if (fLightType == VSP_LIGHT_TYPE_DIRECTIONAL)
    {
        outAttenuation = 1.0f;
        return -VspGetLightDirection(light);
    }

    float3 fToLight = VspGetLightPosition(light) - fWorldPosition;
    float fDistance = length(fToLight);
    float3 fDirectionToLight = VspSafeNormalize(fToLight, -VspGetLightDirection(light));

    outAttenuation = VspGetDistanceAttenuation(fDistance, VspGetLightRange(light));
    if (fLightType == VSP_LIGHT_TYPE_SPOT)
    {
        // The cone is measured against the direction the light points in.
        float fCosAngle = dot(-fDirectionToLight, VspGetLightDirection(light));
        outAttenuation *= VspGetSpotAttenuation(fCosAngle, light.fSpotAngles.x, light.fSpotAngles.y);
    }
    return fDirectionToLight;
}

// Lambert shading of one light at one position.
float3 VspShadeLambertLight(VspSurface surface, VspLight light, float3 fWorldPosition)
{
    float fAttenuation = 1.0f;
    float3 fDirectionToLight = VspGetLightDirectionToSurface(light, fWorldPosition, fAttenuation);
    return VspShadeLambert(surface, fDirectionToLight, VspGetLightRadiance(light) * fAttenuation);
}

// Blinn-Phong shading of one light at one position.
float3 VspShadeBlinnPhongLight(VspSurface surface, VspLight light, float3 fWorldPosition)
{
    float fAttenuation = 1.0f;
    float3 fDirectionToLight = VspGetLightDirectionToSurface(light, fWorldPosition, fAttenuation);
    return VspShadeBlinnPhong(
        surface, fDirectionToLight, VspGetLightRadiance(light) * fAttenuation, surface.fSpecularColor);
}

// Metallic/roughness shading of one light at one position.
float3 VspShadeMetallicRoughnessLight(VspSurface surface, VspLight light, float3 fWorldPosition)
{
    float fAttenuation = 1.0f;
    float3 fDirectionToLight = VspGetLightDirectionToSurface(light, fWorldPosition, fAttenuation);
    return VspShadeMetallicRoughness(surface, fDirectionToLight, VspGetLightRadiance(light) * fAttenuation);
}

// Lambert over a whole light array: what a simple lit shader needs. It takes
// the same arguments as the metallic/roughness version below, so a shader can
// switch models without rewriting the call.
float3 VspShadeLambertLights(
    VspSurface surface,
    VspLight lights[VSP_MAX_LIGHTS],
    uint uLightCount,
    float3 fWorldPosition,
    float3 fAmbient)
{
    float3 color = surface.fAlbedo * fAmbient;
    for (uint uLightIndex = 0; uLightIndex < uLightCount && uLightIndex < VSP_MAX_LIGHTS; ++uLightIndex)
    {
        color += VspShadeLambertLight(surface, lights[uLightIndex], fWorldPosition);
    }
    return color;
}

// Metallic/roughness over a whole light array, which is what a PBR shader
// calls. fWorldPosition is where the surface is; the ambient term is added on
// top of every light.
float3 VspShadeMetallicRoughnessLights(
    VspSurface surface,
    VspLight lights[VSP_MAX_LIGHTS],
    uint uLightCount,
    float3 fWorldPosition,
    float3 fAmbient)
{
    float3 color = surface.fAlbedo * fAmbient;
    for (uint uLightIndex = 0; uLightIndex < uLightCount && uLightIndex < VSP_MAX_LIGHTS; ++uLightIndex)
    {
        color += VspShadeMetallicRoughnessLight(surface, lights[uLightIndex], fWorldPosition);
    }
    return color;
}

#endif // VSP_LIGHTING_INCLUDED
