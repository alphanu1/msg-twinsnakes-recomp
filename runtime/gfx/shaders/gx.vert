/* The base vertex shader: a pass-through.
 *
 * The transform stays on the CPU for now, and that is a deliberate first
 * step rather than a shortcut. `transform` and `project` in raster.c are
 * verified against Dolphin's packed projection field for field, and the
 * position matrix is selected PER VERTEX from XF memory - so doing the
 * transform here would mean reimplementing and re-verifying both at the
 * same time as everything else. Clip-space positions come in; the GPU does
 * the divide, the viewport and the rasterisation, which is the part the CPU
 * is bad at.
 *
 * Moving the transform onto the GPU is worth doing later, and it is a
 * separate change with its own comparison.
 */
#version 450

layout(location = 0) in vec4 in_pos;    /* clip space: x, y, z, w */
layout(location = 1) in vec4 in_colour; /* rgba, 0-1 */
layout(location = 2) in vec2 in_uv;

layout(location = 0) out vec4 v_colour;
layout(location = 1) out vec2 v_uv;

void main()
{
    gl_Position = in_pos;
    v_colour = in_colour;
    v_uv = in_uv;
}
