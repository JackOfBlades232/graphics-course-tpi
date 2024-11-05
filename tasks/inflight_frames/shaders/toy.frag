#version 430
#extension GL_GOOGLE_include_directive : require

#include "UniformParams.h"

layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec4 fragColor;

layout(binding = 0, set = 1) uniform sampler2D   iChannel0;
layout(binding = 1, set = 1) uniform sampler2D   iChannel1;
layout(binding = 2, set = 1) uniform samplerCube iChannel2;
// layout(binding = 3, set = 1) uniform samplerXX iChannel3;


layout(binding = 7) uniform Params
{
  UniformParams params;
};

#define EPS 1e-5
#define PI 3.14159265359

#define SDF_DIST_COEFF 0.4
#define SDF_ITER 1500

mat3 mv = mat3(1.0);

float runion(float d1, float d2) { return min(d1, d2); }
float rinters(float d1, float d2) { return max(d1, d2); }

float smin(float a, float b, float k)
{
  float t = clamp(0.5 + 0.5*(a-b)/k, 0.0, 1.0);
  return mix(a, b, t) - k*t*(1.0-t);
}

float sunion(float d1, float d2, float k) { return smin(d1, d2, k); }

float d_quad(vec3 p, float size)
{
  vec3 mp = mv*p;
  float dx = max(0.0, max(mp.x - size, -size-mp.x));
  float dz = max(0.0, max(mp.z - size, -size-mp.z));
  return length(vec3(dx, mp.y, dz));
}

// Drop = Sphere + circular cone 
// (which is obtained as R3/two tangent circles
//  clipped by two planes)
float d_drop(vec3 p, float y, float r)
{
  vec3 op = mv*p - vec3(0.0, y, 0.0);
  float ds = length(op) - r;

  float pr = length(op.xz);
  vec2 op2 = vec2(pr, op.y);
  vec2 cl = normalize(vec2(-1.0, 1.0)) * 4.0*r;
  vec2 cr = vec2(-cl.x, cl.y);
  float dl = length(cl-op2) - 3.0*r;
  float dr = length(cr-op2) - 3.0*r;

  float dup = op.y - 4.0*r/sqrt(2.0);
  float ddown = r/sqrt(2.0) - op.y;

  float dclamp = pr - 3.0*r;

  return rinters(runion(ds, rinters(rinters(-dl, -dr), rinters(dup, ddown))), dclamp);
}

float get_drop_y(float period, float maxh, float time_off)
{
  return maxh*(1.0-mod((params.iTime + time_off)/period, 2.0));
}

// Base wave amplitude is zero initially, then quickly
// rises linearly as the drop hits the water, and then
// slowly goes back to zero
float calc_drop_base_amp(float period, float maxh, 
    float r, float time_off,
    out float phase_t, out float phase_h)
{
  const float max_amp = 0.4629/3.5;
  const float dying_speed = 0.12/3.0;
  const float rt_offset = 0.05;

  float t = mod(params.iTime + time_off, period*2.0);
  float v = maxh/period;
  float t1 = (maxh-r+rt_offset)/v;
  float t2 = (maxh+r+rt_offset)/v;
  phase_t = t-t1;
  phase_h = phase_t/(period*2.0-t1);
  if (t <= t1)
    return 0.0;
  else if (t <= t2)
    return mix(0.0, max_amp, (t-t1)/(t2-t1));
  else
    return max(max_amp - dying_speed*(t-t2), 0.0);
}

// Sine wave with dying aplitude
float d_drop_wave(float pr, 
    float period, float maxh, float r,
    float time_off)
{
  const float max_pow = 6.0;
  const float min_pow = 1.0;

  // Base amplitude is large when drop lands, and dies linearly with time
  // The fade-out of the waves follows 1/(rad+1)^k formula, where
  // k is large when drop lands (high splash, waves only nearby),
  // but dies down linearly with time

  float phase_t, phase_h;
  float base_amp = calc_drop_base_amp(period, maxh, r, time_off,
      phase_t, phase_h);
  if (base_amp < EPS)
    return 0.0;
  float amp = base_amp/pow(pr+1.0, mix(max_pow, min_pow, phase_h));

  amp = smin(amp, 0.04, 0.02);

  return amp - 
    cos(22.0*(pr-0.25*phase_t/2.0))*amp;
}

float d_wave(float t, float coeff, float amp, float inv_period)
{ return amp * (1.0 - sin(t*coeff + params.iTime*inv_period)); }

float d_regular_waves(vec3 p)
{
  vec3 mp = mv*p;

  float xwave1 = d_wave(mp.x, 7.0, 0.004, 1.0);
  float xwave2 = d_wave(mp.x, 15.0, 0.002, 5.0);
  float xwave3 = d_wave(mp.x, 5.0, 0.004, 0.5);
  float xwave4 = d_wave(mp.x, 1.0, 0.004, 1.7);
  float zwave1 = d_wave(mp.z, 7.0, 0.004, 1.0);
  float zwave2 = d_wave(mp.z, 15.0, 0.002, 5.0);
  float zwave3 = d_wave(mp.z, 5.0, 0.0004, 0.5);
  float zwave4 = d_wave(mp.z, 1.0, 0.004, 1.7);
  float xzwave1 = d_wave(mp.x-mp.z, 7.0, 0.002, 1.0);
  float xzwave2 = d_wave(mp.x-mp.z, 15.0, 0.0005, 5.0);
  float xzwave3 = d_wave(mp.x-mp.z, 5.0, 0.01, 0.5);
  float xzwave4 = d_wave(mp.x-mp.z, 1.0, 0.0018, 1.7);

  return xwave1 + xwave2 + xwave3 + xwave4 +
    zwave1 + zwave2 + zwave3 + zwave4 +
    xzwave1 + xzwave2 + xzwave3 + xzwave4;
}

float sdf(vec3 p)
{
  const float period = 6.0;
  const float maxh = 2.0;
  const float r = 0.13;

  float pr = length((mv*p).xz);

  // 4 drops and a wavy quad

  float drop1_offset = 0.0;
  float drop2_offset = 3.0;
  float drop3_offset = 6.0;
  float drop4_offset = 9.0;

  float dd1 = d_drop(p, get_drop_y(period, maxh, drop1_offset), r);
  float dd2 = d_drop(p, get_drop_y(period, maxh, drop2_offset), r);
  float dd3 = d_drop(p, get_drop_y(period, maxh, drop3_offset), r);
  float dd4 = d_drop(p, get_drop_y(period, maxh, drop4_offset), r);

  float drop_waves = d_drop_wave(pr, period, maxh, r, drop1_offset) +
    d_drop_wave(pr, period, maxh, r, drop2_offset) +
    d_drop_wave(pr, period, maxh, r, drop3_offset) +
    d_drop_wave(pr, period, maxh, r, drop4_offset);

  float dq = d_quad(p, 3.0) - 1.5*drop_waves - d_regular_waves(p);

  float drops = runion(dd1, runion(dd2, runion(dd3, dd4)));
  return sunion(dq, drops, 0.05);
}

vec3 trace(vec3 orig, vec3 dir, out bool hit)
{
  float tot_d = 0.0;
  hit = false;
  for (int i = 0; i < SDF_ITER; i++) {
    float d = sdf(orig);
    if (d < 0.001) {
      hit = true;
      break;
    }
    d *= SDF_DIST_COEFF;
    tot_d += d;
    if (tot_d > 20.0)
      break;
    orig += d*dir;

  }
  return orig;
}

vec3 gen_normal(vec3 p, float d)
{
  float e = max(d*0.5, EPS);

  vec3 dx = vec3(e, 0.0, 0.0);
  vec3 dy = vec3(0.0, e, 0.0);
  vec3 dz = vec3(0.0, 0.0, e);
  float grx = sdf(p+dx) - sdf(p-dx);
  float gry = sdf(p+dy) - sdf(p-dy);
  float grz = sdf(p+dz) - sdf(p-dz);

  return normalize(vec3(grx, gry, grz));
}

vec3 get_triplanar_weights(vec3 n)
{
  vec3 w = n*n;
  return w / (w.x + w.y + w.z);
}

void mainImage(out vec4 frag_color, in vec2 frag_coord)
{
  vec2 uv = (frag_coord - 0.5*params.iResolution.xy)/params.iResolution.x;
  vec3 eye = vec3(0.0, 0.0, -5.0);
  vec3 dir = normalize(vec3(uv, 1.0));

  vec3 light = vec3(-0.4, 0.4, -5.0);

  // Set default pos to 30/60 degrees from center, otherwise use mouse
  vec2 mouse_pos = vec2(params.iMouse.x > 0.0 ? params.iMouse.x : params.iResolution.x*0.5 + 30.0,
      params.iMouse.y > 0.0 ? params.iMouse.y : params.iResolution.y*0.5 + 60.0);

  float xrad = PI*((mouse_pos.x/params.iResolution.x) - 0.5);
  float yrad = PI*((mouse_pos.y/params.iResolution.y) - 0.5);
  float xcos = cos(xrad);
  float xsin = sin(xrad);
  float ycos = cos(yrad);
  float ysin = sin(yrad);
  mv = inverse(mat3(xcos, 0.0, -xsin,
        0.0,  1.0, 0.0,
        xsin, 0.0, xcos) *
      mat3(1.0, 0.0,  0.0,
        0.0, ycos, -ysin,
        0.0, ysin, ycos));

  bool hit;
  vec3 p = trace(eye, dir, hit); 

  if (hit) {
    // Light blue
    vec3 base_rgb = 2.0*vec3(0.019, 0.764, 0.866);

    vec3 n = gen_normal(p, 0.001);
    vec3 l = normalize(light - p);
    float nl = max(dot(n, l), 0.0);
    vec4 notex_col = vec4(nl * base_rgb, 1.0);

    vec3 w = get_triplanar_weights(n);
    vec4 cx = texture(iChannel1, p.yz);
    vec4 cy = texture(iChannel1, p.zx);
    vec4 cz = texture(iChannel1, p.xy);
    vec4 trip_color = notex_col * (w.x*cx + w.y*cy + w.z*cz);

    if ((mv*p).y < 0.2)
      frag_color = trip_color * texture(iChannel0, (mv*p).zx/6.0 + 0.5);
    else
      frag_color = trip_color;
  } else {
    // Cubemap sample
    frag_color = texture(iChannel2, mv*dir);
  }
}

void main()
{
  mainImage(fragColor, texCoord * params.iResolution);
}