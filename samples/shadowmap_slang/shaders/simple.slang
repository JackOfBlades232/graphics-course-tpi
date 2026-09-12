struct VsInput
{
  let vPosNorm:         float4 : POSITION;
  let vTexCoordAndTang: float4 : UV;
};

struct Surface
{
  var wPos:     float3;
  var wNorm:    float3;
  var wTangent: float3;
  var texCoord: float2;
};

struct VsOutput
{
  var pos:  float4  : SV_Position;
  var surf: Surface : SURFACE;
};

struct PsOutput
{
  var color: float4 : SV_Target;
};

func inverse(m: float4x4) -> float4x4
{
  let n11 = m[0][0];
  let n12 = m[1][0];
  let n13 = m[2][0];
  let n14 = m[3][0];
  let n21 = m[0][1];
  let n22 = m[1][1];
  let n23 = m[2][1];
  let n24 = m[3][1];
  let n31 = m[0][2];
  let n32 = m[1][2];
  let n33 = m[2][2];
  let n34 = m[3][2];
  let n41 = m[0][3];
  let n42 = m[1][3];
  let n43 = m[2][3];
  let n44 = m[3][3];

  let t11 = n23 * n34 * n42 - n24 * n33 * n42 + n24 * n32 * n43 - n22 * n34 * n43 - n23 * n32 * n44 + n22 * n33 * n44;
  let t12 = n14 * n33 * n42 - n13 * n34 * n42 - n14 * n32 * n43 + n12 * n34 * n43 + n13 * n32 * n44 - n12 * n33 * n44;
  let t13 = n13 * n24 * n42 - n14 * n23 * n42 + n14 * n22 * n43 - n12 * n24 * n43 - n13 * n22 * n44 + n12 * n23 * n44;
  let t14 = n14 * n23 * n32 - n13 * n24 * n32 - n14 * n22 * n33 + n12 * n24 * n33 + n13 * n22 * n34 - n12 * n23 * n34;

  let det = n11 * t11 + n21 * t12 + n31 * t13 + n41 * t14;
  let idet = 1.f / det;

  var ret: float4x4;

  ret[0][0] = t11 * idet;
  ret[0][1] = (n24 * n33 * n41 - n23 * n34 * n41 - n24 * n31 * n43 + n21 * n34 * n43 + n23 * n31 * n44 - n21 * n33 * n44) * idet;
  ret[0][2] = (n22 * n34 * n41 - n24 * n32 * n41 + n24 * n31 * n42 - n21 * n34 * n42 - n22 * n31 * n44 + n21 * n32 * n44) * idet;
  ret[0][3] = (n23 * n32 * n41 - n22 * n33 * n41 - n23 * n31 * n42 + n21 * n33 * n42 + n22 * n31 * n43 - n21 * n32 * n43) * idet;

  ret[1][0] = t12 * idet;
  ret[1][1] = (n13 * n34 * n41 - n14 * n33 * n41 + n14 * n31 * n43 - n11 * n34 * n43 - n13 * n31 * n44 + n11 * n33 * n44) * idet;
  ret[1][2] = (n14 * n32 * n41 - n12 * n34 * n41 - n14 * n31 * n42 + n11 * n34 * n42 + n12 * n31 * n44 - n11 * n32 * n44) * idet;
  ret[1][3] = (n12 * n33 * n41 - n13 * n32 * n41 + n13 * n31 * n42 - n11 * n33 * n42 - n12 * n31 * n43 + n11 * n32 * n43) * idet;

  ret[2][0] = t13 * idet;
  ret[2][1] = (n14 * n23 * n41 - n13 * n24 * n41 - n14 * n21 * n43 + n11 * n24 * n43 + n13 * n21 * n44 - n11 * n23 * n44) * idet;
  ret[2][2] = (n12 * n24 * n41 - n14 * n22 * n41 + n14 * n21 * n42 - n11 * n24 * n42 - n12 * n21 * n44 + n11 * n22 * n44) * idet;
  ret[2][3] = (n13 * n22 * n41 - n12 * n23 * n41 - n13 * n21 * n42 + n11 * n23 * n42 + n12 * n21 * n43 - n11 * n22 * n43) * idet;

  ret[3][0] = t14 * idet;
  ret[3][1] = (n13 * n24 * n31 - n14 * n23 * n31 + n14 * n21 * n33 - n11 * n24 * n33 - n13 * n21 * n34 + n11 * n23 * n34) * idet;
  ret[3][2] = (n14 * n22 * n31 - n12 * n24 * n31 - n14 * n21 * n32 + n11 * n24 * n32 + n12 * n21 * n34 - n11 * n22 * n34) * idet;
  ret[3][3] = (n12 * n23 * n31 - n13 * n22 * n31 + n13 * n21 * n32 - n11 * n23 * n32 - n12 * n21 * n33 + n11 * n22 * n33) * idet;

  return ret;
}

func decode_normal(a_data: uint) -> float3 
{
  let a_enc_x = (a_data  & 0x0000FFFFu);
  let a_enc_y = ((a_data & 0xFFFF0000u) >> 16);
  let sgn     = (a_enc_x & 0x0001u) != 0 ? -1.f : 1.f;

  let usX = int(a_enc_x & 0x0000FFFEu);
  let usY = int(a_enc_y & 0x0000FFFFu);

  let sX  = (usX <= 32767) ? usX : usX - 65536;
  let sY  = (usY <= 32767) ? usY : usY - 65536;

  let x = sX * (1.f / 32767.f);
  let y = sY * (1.f / 32767.f);
  let z = sgn * sqrt(max(1.0f - x * x - y * y, 0.f));

  return float3(x, y, z);
}

[shader("vertex")]
func vertexMain(
  input: VsInput,
  uniform mProjView: float4x4,
  uniform mModel: float4x4) -> VsOutput
{
  var vOut: VsOutput = {};

  let wNorm = float4(decode_normal(asint(input.vPosNorm.w)),         0.f);
  let wTang = float4(decode_normal(asint(input.vTexCoordAndTang.z)), 0.f);

  vOut.surf.wPos = mul(mModel, float4(input.vPosNorm.xyz, 1.f)).xyz;
  vOut.surf.wNorm = normalize(mul(float3x3(transpose(inverse(mModel))), wNorm.xyz));
  vOut.surf.wTangent = normalize(mul(float3x3(transpose(inverse(mModel))), wTang.xyz));
  vOut.surf.texCoord = input.vTexCoordAndTang.xy;

  vOut.pos = mul(mProjView, float4(vOut.surf.wPos, 1.f));

  return vOut;
}

#include "UniformParams.h"

[shader("fragment")]
func fragmentMain(
  surf: Surface : SURFACE,
  params: ConstantBuffer<UniformParams>,
  shadowMap: Sampler2D<float>) -> PsOutput
{
  let posLightClipSpace = mul(params.lightMatrix, float4(surf.wPos, 1.f));

  // for orto matrix, we don't need perspective division, you can remove it if you want; this is general case;
  let posLightSpaceNDC = posLightClipSpace.xyz / posLightClipSpace.w;

  // just shift coords from [-1,1] to [0,1]
  let shadowTexCoord = posLightSpaceNDC.xy * 0.5f + float2(0.5f, 0.5f);

  let outOfView = (shadowTexCoord.x < 0.0001f || shadowTexCoord.x > 0.9999f || shadowTexCoord.y < 0.0091f || shadowTexCoord.y > 0.9999f);
  let shadow    = ((posLightSpaceNDC.z < shadowMap.SampleLevel(shadowTexCoord, 0.f) + 0.001f) || outOfView) ? 1.0f : 0.0f;

  let dark_violet = float4(0.59f, 0.f, 0.82f, 1.f);
  let chartreuse  = float4(0.5f, 1.f, 0.0f, 1.f);

  let lightColor1 = lerp(dark_violet, chartreuse, abs(sin(params.time)));
  let lightColor2 = float4(1.f, 1.f, 1.f, 1.f);

  let lightDir   = normalize(params.lightPos - surf.wPos);
  let lightColor = max(dot(surf.wNorm, lightDir), 0.f) * lightColor1;
  let ambient = 0.05;

  // Light formula is pretty arbitrary and most definitely wrong
  return PsOutput((lightColor * shadow + ambient) * float4(params.baseColor, 1.f));
}
