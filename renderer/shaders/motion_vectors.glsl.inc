#ifndef MOTION_VECTORS_H_INCLUDED
#define MOTION_VECTORS_H_INCLUDED

void get_static_pixel_motion_vector(
  vec2 frag_coord,
  vec2 res,
  vec2 prev_ndc_coord,
  vec2 cur_jitter,
  vec2 prev_jitter,
  out vec3 out_mv)
{
  vec2 cur_uv_unjittered = frag_coord / res - cur_jitter;
  vec2 prev_uv_unjittered = 0.5f * prev_ndc_coord + 0.5f - prev_jitter;
  out_mv = vec3(cur_uv_unjittered - prev_uv_unjittered, 0.f);
}

#endif
