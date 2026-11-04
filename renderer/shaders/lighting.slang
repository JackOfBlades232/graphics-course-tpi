import constants;
import common;

[shader("compute")]
[numthreads(1, 1, 1)]
func transfer_light_matrix(
  lightMats: RWStructuredBuffer<LightMatrices>,
  viewParams: ConstantBuffer<ViewParams>,
  viewData: StructuredBuffer<ViewData>,
  uniform lightId: uint)
{
  let correctedMat = adjust_viewproj_mat(viewParams, viewData[0]);
  if (lightId < POINT_LIGHT_BUF_SIZE * 6)
  {
    let idx = lightId;
    let lidx = idx / 6;
    let fidx = idx - lidx * 6;
    lightMats[0].pointLightMats[lidx][fidx] = correctedMat;
  }
  else if (lightId < POINT_LIGHT_BUF_SIZE * 6 + SPOT_LIGHT_BUF_SIZE)
  {
    let idx = lightId - POINT_LIGHT_BUF_SIZE * 6;
    lightMats[0].spotLightMats[idx] = correctedMat;
  }
  else if (
    lightId <
    POINT_LIGHT_BUF_SIZE * 6 + SPOT_LIGHT_BUF_SIZE + DIRECTIONAL_LIGHT_BUF_SIZE * CSM_CASCADE_COUNT)
  {
    let idx = lightId - POINT_LIGHT_BUF_SIZE * 6 - SPOT_LIGHT_BUF_SIZE;
    let lidx = idx / CSM_CASCADE_COUNT;
    let cidx = idx - lidx * CSM_CASCADE_COUNT;
    lightMats[0].directionalLightMats[lidx][cidx] = correctedMat;
  }
}

