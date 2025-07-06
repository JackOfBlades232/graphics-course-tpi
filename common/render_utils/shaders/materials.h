#ifndef MATERIALS_H_INCLUDED
#define MATERIALS_H_INCLUDED

#include "cpp_glsl_compat.h"

#ifdef __cplusplus

enum class MaterialType : shader_uint
{
  PBR = 1,
  DIFFUSE = 2 //  @TODO: add spec/gloss
};

enum class TexId : uint16_t
{
  INVALID = uint16_t(-1)
};

enum class SmpId : uint16_t
{
  INVALID = uint16_t(-1)
};

enum class TexSmpIdPair : uint32_t
{
  INVALID = uint32_t(-1)
};

inline TexSmpIdPair pack_tex_smp_id_pair(TexId tid, SmpId sid)
{
  return TexSmpIdPair{(uint32_t(sid) << 16) | uint32_t(tid)};
}

enum class MaterialId : uint32_t
{
  INVALID = uint32_t(-1)
};

#define NO_TEXTURE_ID pack_tex_smp_id_pair(TexId::INVALID, SmpId::INVALID)

#define MATERIAL_MISSING 0
#define MATERIAL_PBR MaterialType::PBR
#define MATERIAL_DIFFUSE MaterialType::DIFFUSE

#else

#define MaterialType shader_uint
#define MaterialId shader_uint
#define TexSmpIdPair shader_uint

#define NO_TEXTURE_ID TexSmpIdPair(-1)
#define NO_MATERIAL MaterialId(-1)

#define MATERIAL_PBR MaterialType(1)
#define MATERIAL_DIFFUSE MaterialType(2)

uvec2 unpack_tex_smp_id_pair(TexSmpIdPair packed) 
{
  const uint t = packed & 0xFFFF;
  const uint s = (packed >> 16) & 0xFFFF;
  return uvec2(t, s);
}

#endif

// @TODO add emission and occlusion

// @TODO pack at least somehow
struct Material
{
  MaterialType mat;

  // Common
  TexSmpIdPair normalTexSmp; 
  
  // PBR
  shader_uint baseColorFactor;
  float metalnessFactor;
  float roughnessFactor;
  TexSmpIdPair baseColorTexSmp; 
  TexSmpIdPair metalnessRoughnessTexSmp; 

  // Diffuse @NOTE: can be aliased w/ PBR
  shader_uint diffuseColorFactor;
  //shader_uint specularFactor;
  //float glossinessFactor;
  TexSmpIdPair diffuseTexSmp; 
  //TexSmpIdPair specularGlossinessTexSmp; 

  TexSmpIdPair heightDisplacementTexSmp;
};

#endif // MATERIALS_H_INCLUDED
