/* The base fragment shader: the rasterised colour times one texture.
 *
 * This is the single most common TEV configuration in this game and NOT a
 * general combiner - the generator that turns an arbitrary TEV state into
 * GLSL is the next piece of work. Standing this up first gives the pipeline,
 * the vertex layout, the sampler binding and the readback comparison
 * something to be checked against before any of that exists.
 *
 * SET 2, NOT SET 0. SDL_gpu.h gives the SPIR-V binding tables TWICE and
 * they are different: the one by SDL_CreateGPUComputePipeline says "0:
 * sampled textures", and the one by SDL_CreateGPUShader - the graphics one,
 * which is this - says vertex shaders use set 0 and FRAGMENT shaders use
 * SET 2 for sampled textures, with set 3 for their uniform buffers.
 *
 * Getting it wrong is quiet. The pipeline is created, the draw is accepted,
 * the geometry rasterises, and `texture()` returns vec4(0) - so the triangle
 * appears in black and looks like a vertex-colour or upload fault. Only the
 * debug device names it: "uses descriptor [Set 0, Binding 0, variable
 * u_tex] but the binding was not declared in
 * VkPipelineLayoutCreateInfo::pSetLayouts[0]".
 */
#version 450

layout(location = 0) in vec4 v_colour;
layout(location = 1) in vec2 v_uv[4];

layout(location = 0) out vec4 out_colour;

layout(set = 2, binding = 0) uniform sampler2D u_tex[4];

void main()
{
    out_colour = v_colour * texture(u_tex[0], v_uv[0]);
}
