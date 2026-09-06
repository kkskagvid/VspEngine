#version 450
#extension GL_EXT_nonuniform_qualifier : require

// Bindless descriptor: one large array of sampled images, indexed dynamically.
layout(set = 0, binding = 1) uniform sampler2D uBindlessTextureArray[];

layout(location = 0) in vec4 inColor;
layout(location = 1) in vec2 inUv;

layout(push_constant) uniform PushConstants
{
    vec4 uOverrideColor;   // applied when uColorMode != 3
    int  uColorMode;       // 0 red, 1 blue, 2 green, 3 multicolor (vertex colors)
    uint uTextureIndex;    // bindless texture slot, read with nonuniformEXT
} pc;

layout(location = 0) out vec4 outColor;

void main()
{
    vec4 baseColor = (pc.uColorMode == 3) ? inColor : pc.uOverrideColor;
    outColor = baseColor * texture(uBindlessTextureArray[nonuniformEXT(pc.uTextureIndex)], inUv);
}
