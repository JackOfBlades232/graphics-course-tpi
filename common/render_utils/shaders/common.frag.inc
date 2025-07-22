#ifndef SHADER_COMMON_FRAG_INC_INCLUDED
#define SHADER_COMMON_FRAG_INC_INCLUDED

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

// @TODO: update with knowledge, just slapped this in from an older repo
// Thx to Ian McEwan
vec3 mod289(vec3 x) { return x - floor(x * (1.0 / 289.0)) * 289.0; }
vec2 mod289(vec2 x) { return x - floor(x * (1.0 / 289.0)) * 289.0; }
vec3 permute(vec3 x) { return mod289(((x*34.0)+1.0)*x); }

// @TODO seed
float simplex_noise(vec2 v)
{
  const vec4 C = vec4(0.211324865405187, 0.366025403784439, -0.577350269189626, 0.024390243902439);

  vec2 i  = floor(v + dot(v, C.yy));
  vec2 x0 = v - i + dot(i, C.xx);

  vec2 i1 = vec2(0.0);
  i1 = (x0.x > x0.y) ? vec2(1.0, 0.0) : vec2(0.0, 1.0);
  vec2 x1 = x0.xy + C.xx - i1;
  vec2 x2 = x0.xy + C.zz;

  i = mod289(i);
  vec3 p = permute(permute(i.y + vec3(0.0, i1.y, 1.0)) + i.x + vec3(0.0, i1.x, 1.0));

  vec3 m = max(0.5 - vec3(dot(x0,x0), dot(x1,x1), dot(x2,x2)), 0.0);

  m = m*m;
  m = m*m;

  vec3 x = 2.0 * fract(p * C.www) - 1.0;
  vec3 h = abs(x) - 0.5;
  vec3 ox = floor(x + 0.5);
  vec3 a0 = x - ox;

  m *= 1.79284291400159 - 0.85373472095314 * (a0*a0+h*h);

  vec3 g = vec3(0.0);
  g.x  = a0.x * x0.x + h.x * x0.y;
  g.yz = a0.yz * vec2(x1.x,x2.x) + h.yz * vec2(x1.y,x2.y);
  return 130.0 * dot(m, g);
}

#endif // SHADER_COMMON_FRAG_INC_INCLUDED
