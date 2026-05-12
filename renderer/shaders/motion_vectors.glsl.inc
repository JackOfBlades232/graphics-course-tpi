#ifndef MOTION_VECTORS_H_INCLUDED
#define MOTION_VECTORS_H_INCLUDED

void get_static_pixel_motion_vector(
  vec2 frag_coord, vec2 res, vec2 prev_ndc_coord, out vec2 out_mv)
{
  vec2 cur_uv = frag_coord / res;
  vec2 prev_uv = 0.5f * prev_ndc_coord + 0.5f;
  out_mv = cur_uv - prev_uv;
}

#endif
