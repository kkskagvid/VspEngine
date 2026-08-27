#version 450

// Vulkan 1.2 fallback path: classic combined image sampler binding.
layout(set = 0, binding = 1) uniform sampler2D uColorTexture;

layout(location = 0) in vec4 inColor;
layout(location = 1) in vec2 inUv;

layout(push_constant) uniform PushConstants
{
    vec4 uOverrideColor;   // applied when uColorMode != 3
    int  uColorMode;       // 0 red, 1 blue, 2 green, 3 multicolor (vertex colors)
    uint uTextureIndex;    // unused on the fallback path (kept for API symmetry)
} pc;

layout(location = 0) out vec4 outColor;

void main()
{
    vec4 baseColor = (pc.uColorMode == 3) ? inColor : pc.uOverrideColor;
    outColor = baseColor * texture(uColorTexture, inUv);
}
