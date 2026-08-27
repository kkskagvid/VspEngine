#version 450

// Vulkan 1.2 fallback path: classic descriptor set with one uniform buffer.
layout(set = 0, binding = 0) uniform CameraUniformBuffer
{
    mat4 uViewProjectionMatrix;
} camera;

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec2 inUv;

layout(location = 0) out vec4 outColor;
layout(location = 1) out vec2 outUv;

void main()
{
    gl_Position = camera.uViewProjectionMatrix * vec4(inPosition, 0.0, 1.0);
    outColor = inColor;
    outUv = inUv;
}
