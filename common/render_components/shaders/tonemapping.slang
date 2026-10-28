func gamma_encode(ldr: float3) -> float3
{
  return pow(ldr, float3(1.f / 2.2f));
}

func gamma_encode_conditional(ldr: float3, cond: bool) -> float3
{
  if (cond)
    return gamma_encode(ldr);
  else
    return ldr;
}

func encode_tonemapping_result(ldr: float3, consts: Constants) -> float4
{
  return float4(gamma_encode_conditional(ldr, consts.gammaEncodeInTonemapping != 0), 1.f);
}

func encode_aa_result(ldr: float3, consts: Constants) -> float4
{
  return float4(gamma_encode_conditional(ldr, consts.gammaEncodeInTonemapping == 0), 1.f);
}
