#version 450
#extension GL_EXT_nonuniform_qualifier : require

// Bindless path: the camera lives in a descriptor-set uniform buffer.
layout(set = 0, binding = 0) uniform CameraUniformBuffer
{
    mat4 uViewProjectionMatrix;
} camera;

// Push constants shared with the fragment stage (explicit offsets keep the
// block byte-identical to the C++ PushConstants struct, 32 bytes total).
layout(push_constant) uniform PushConstants
{
    layout(offset = 0)  vec2 uPositionOffset;    // per-draw position offset
    layout(offset = 8)  vec2 uOverrideColorRG;   // unused by the vertex stage
    layout(offset = 16) vec2 uOverrideColorBA;   // unused by the vertex stage
    layout(offset = 24) int  uColorMode;         // unused by the vertex stage
    layout(offset = 28) uint uTextureIndex;      // unused by the vertex stage
} pc;

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec2 inUv;

layout(location = 0) out vec4 outColor;
layout(location = 1) out vec2 outUv;

void main()
{
    gl_Position = camera.uViewProjectionMatrix * vec4(inPosition + pc.uPositionOffset, 0.0, 1.0);
    outColor = inColor;
    outUv = inUv;
}
