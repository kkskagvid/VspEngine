#version 450
#extension GL_EXT_nonuniform_qualifier : require

// Bindless descriptor: one large array of sampled images, indexed dynamically.
layout(set = 0, binding = 1) uniform sampler2D uBindlessTextureArray[];

layout(location = 0) in vec4 inColor;
layout(location = 1) in vec2 inUv;

// Push constants shared with the vertex stage (explicit offsets keep the
// block byte-identical to the C++ PushConstants struct, 32 bytes total).
layout(push_constant) uniform PushConstants
{
    layout(offset = 0)  vec2 uPositionOffset;    // unused by the fragment stage
    layout(offset = 8)  vec2 uOverrideColorRG;   // applied when uColorMode != 3
    layout(offset = 16) vec2 uOverrideColorBA;   // applied when uColorMode != 3
    layout(offset = 24) int  uColorMode;         // 0 red, 1 blue, 2 green, 3 multicolor
    layout(offset = 28) uint uTextureIndex;      // bindless texture slot, read with nonuniformEXT
} pc;

layout(location = 0) out vec4 outColor;

void main()
{
    vec4 baseColor = (pc.uColorMode == 3) ? inColor : vec4(pc.uOverrideColorRG, pc.uOverrideColorBA);
    outColor = baseColor * texture(uBindlessTextureArray[nonuniformEXT(pc.uTextureIndex)], inUv);
}
