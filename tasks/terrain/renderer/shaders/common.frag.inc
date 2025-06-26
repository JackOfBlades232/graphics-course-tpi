#ifndef COMMON_H_INCLUDED
#define COMMON_H_INCLUDED

// @TODO: read why this works lel
vec4 catmull_rom_cubic_coeffs(float t)
{
  const float tt = t * t;
  const float ttt = tt * t;

  const float q0 = 0.5f * (-ttt + 2.f * tt - t);
  const float q1 = 0.5f * (3.f * ttt - 5.f * tt + 2.f);
  const float q2 = 0.5f * (-3.f * ttt + 4.f * tt + t);
  const float q3 = 0.5f * (ttt - tt);
  return vec4(q0, q1, q2, q3);
}

#endif // COMMON_H_INCLUDED
