import common;
import constants;

struct InstanceViewRelatedData
{
  var minZ: float;
  var maxZ: float;
};

func point_is_in_frustum(p: float3) -> bool
{
  return p.x >= -1.f && p.x <= 1.f && p.y >= -1.f && p.y <= 1.f && p.z >= 0.f && p.z <= 1.f;
}

func world_to_clip(v: float4, view_params: ViewParams) -> float3
{
  const float4 hom = mul(v, view_params.mProjView);
  return (hom / hom.w).xyz;
}

func world_to_view(v: float4, view_params: ViewParams) -> float3
{
  return mul(v, view_params.mView).xyz;
}

func project_obb_onto_ax(
    c: float3,
    x_ax: float3, y_ax: float3, z_ax: float3,
    exts: float3, m: float3) -> float2
{
  let mc = dot(m, c);
  let rad = 
    abs(dot(m, x_ax)) * exts.x +
    abs(dot(m, y_ax)) * exts.y +
    abs(dot(m, z_ax)) * exts.z;
  let obbMin = mc - rad;
  let obbMax = mc + rad;
  return float2(obbMin, obbMax);
}

func project_frustum_onto_ax(
    x_near: float, y_near: float, z_near: float, z_far: float,
    m: float3, view_params: ViewParams) -> float2
{
  if (view_params.type == ViewType::PERSPECTIVE)
  {
    let p = x_near * abs(m.x) + y_near * abs(m.y);
    let nmd = z_near * m.z;
    var frustumMin: float = nmd - p;
    var frustumMax: float = nmd + p;
    if (frustumMin < 0.f)
      frustumMin *= z_far / z_near;
    if (frustumMax > 0.f)
      frustumMax *= z_far / z_near;
    return float2(frustumMin, frustumMax);
  }
  else // ORTHO
  {
    return project_obb_onto_ax(
      float3(
        0.f,
        0.f,
        (view_params.viewFrustum.nearZ + view_params.viewFrustum.farZ) * 0.5f),
      float3(1.f, 0.f, 0.f),
      float3(0.f, 1.f, 0.f),
      float3(0.f, 0.f, 1.f),
      float3(
        view_params.viewFrustum.nearX,
        view_params.viewFrustum.nearY,
        (view_params.viewFrustum.farZ - view_params.viewFrustum.nearZ) * 0.5f),
      m);
  }
}

func instance_is_in_view(
  bbox: BBox, inst_mat: float4x4, mode: CullingMode, view_params: ViewParams,
  out InstanceViewRelatedData payload) -> bool
{
  if (view_params.needDepthBounds)
  {
    let vps: float3[8] = {
     world_to_view(mul(bbox.min, inst_mat), view_params),
     world_to_view(mul(float4(bbox.min.x, bbox.min.y, bbox.max.z, 1.f), inst_mat), view_params),
     world_to_view(mul(float4(bbox.min.x, bbox.max.y, bbox.min.z, 1.f), inst_mat), view_params),
     world_to_view(mul(float4(bbox.max.x, bbox.min.y, bbox.min.z, 1.f), inst_mat), view_params),
     world_to_view(mul(float4(bbox.max.x, bbox.min.y, bbox.max.z, 1.f), inst_mat), view_params),
     world_to_view(mul(float4(bbox.min.x, bbox.max.y, bbox.max.z, 1.f), inst_mat), view_params),
     world_to_view(mul(float4(bbox.max.x, bbox.max.y, bbox.min.z, 1.f), inst_mat), view_params),
     world_to_view(mul(bbox.max, inst_mat), view_params)};
    payload.minZ = payload.maxZ = vps[0].z;
    for (int i = 1; i < 8; ++i)
    {
      payload.minZ = min(payload.minZ, vps[i].z);
      payload.maxZ = max(payload.maxZ, vps[i].z);
    }
  }

  if (mode == CullingMode::PER_VERTEX)
  {
    let p1 = world_to_clip(mul(bbox.min, inst_mat), view_params);
    let p2 = world_to_clip(mul(float4(bbox.min.x, bbox.min.y, bbox.max.z, 1.f), inst_mat), view_params);
    let p3 = world_to_clip(mul(float4(bbox.min.x, bbox.max.y, bbox.min.z, 1.f), inst_mat), view_params);
    let p4 = world_to_clip(mul(float4(bbox.max.x, bbox.min.y, bbox.min.z, 1.f), inst_mat), view_params);
    let p5 = world_to_clip(mul(float4(bbox.max.x, bbox.min.y, bbox.max.z, 1.f), inst_mat), view_params);
    let p6 = world_to_clip(mul(float4(bbox.min.x, bbox.max.y, bbox.max.z, 1.f), inst_mat), view_params);
    let p7 = world_to_clip(mul(float4(bbox.max.x, bbox.max.y, bbox.min.z, 1.f), inst_mat), view_params);
    let p8 = world_to_clip(mul(bbox.max, inst_mat), view_params);
    return point_is_in_frustum(p1)
      || point_is_in_frustum(p2)
      || point_is_in_frustum(p3)
      || point_is_in_frustum(p4)
      || point_is_in_frustum(p5)
      || point_is_in_frustum(p6)
      || point_is_in_frustum(p7)
      || point_is_in_frustum(p8);
  }
  else if (mode == CullingMode::SAT)
  {
    // https://bruop.github.io/improved_frustum_culling/

    let newMin = float3(
      min(bbox.min.x, bbox.max.x - BIG_EPSILON),
      min(bbox.min.y, bbox.max.y - BIG_EPSILON),
      min(bbox.min.z, bbox.max.z - BIG_EPSILON));
    let newMax = float3(
      max(bbox.max.x, bbox.min.x + BIG_EPSILON),
      max(bbox.max.y, bbox.min.y + BIG_EPSILON),
      max(bbox.max.z, bbox.min.z + BIG_EPSILON));

    let extBbox: BBox = {
      float4(newMin, 1.f),
      float4(newMax, 1.f)};

    let bbase = world_to_view(mul(extBbox.min, inst_mat), view_params);
    let bx = world_to_view(mul(float4(extBbox.max.x, extBbox.min.y, extBbox.min.z, 1.f), inst_mat), view_params) - bbase;
    let by = world_to_view(mul(float4(extBbox.min.x, extBbox.max.y, extBbox.min.z, 1.f), inst_mat), view_params) - bbase;
    let bz = world_to_view(mul(float4(extBbox.min.x, extBbox.min.y, extBbox.max.z, 1.f), inst_mat), view_params) - bbase;
    let e = float3(length(bx), length(by), length(bz));
    let obb: OBBox = {
      bbase + (bx + by + bz) * 0.5f, e * 0.5f,
      bx / e.x, by / e.y, bz / e.z};

    let xNear = view_params.viewFrustum.nearX;
    let yNear = view_params.viewFrustum.nearY;
    let zNear = view_params.viewFrustum.nearZ;
    let zFar = view_params.viewFrustum.farZ;
   
    // Frustum Z normal
    {
      let m = float3(0.f, 0.f, 1.f);
      let mc = obb.center.z;
      let rad = 
        abs(obb.xAxis.z) * obb.extents.x +
        abs(obb.yAxis.z) * obb.extents.y +
        abs(obb.zAxis.z) * obb.extents.z;
      let obbMin = mc - rad;
      let obbMax = mc + rad;
      let frustumMin = zNear;
      let frustumMax = zFar;
      if (obbMin > frustumMax || obbMax < frustumMin)
        return false;
    }

    // Frustum side normals
    if (view_params.type == ViewType::PERSPECTIVE)
    {
      let ms: float3[4] = {
        float3(zNear, 0.f, xNear),
        float3(-zNear, 0.f, xNear),
        float3(0.f, zNear, yNear),
        float3(0.f, -zNear, yNear)};

      for (var i: uint  = 0; i < 4; ++i)
      {
        let obbInterval = project_obb_onto_ax(
          obb.center, obb.xAxis, obb.yAxis, obb.zAxis, obb.extents, ms[i]);
        let frustumInterval = project_frustum_onto_ax(
          xNear, yNear, zNear, zFar, ms[i], view_params);

        if (obbInterval.x > frustumInterval.y || obbInterval.y < frustumInterval.x)
          return false;
      }
    }
    else // ORTHO
    {
      let ms: float3[2] = {float3(1.f, 0.f, 0.f), float3(0.f, 1.f, 0.f)};

      for (var i: uint = 0; i < 2; ++i)
      {
        let obbInterval = project_obb_onto_ax(
          obb.center, obb.xAxis, obb.yAxis, obb.zAxis, obb.extents, ms[i]);
        let frustumInterval = project_frustum_onto_ax(
          xNear, yNear, zNear, zFar, ms[i], view_params);

        if (obbInterval.x > frustumInterval.y || obbInterval.y < frustumInterval.x)
          return false;
      }
    }

    let axes: float3[3] = {obb.xAxis, obb.yAxis, obb.zAxis};
    let extents: float[3] = {obb.extents.x, obb.extents.y, obb.extents.z};

    // OBB axes (i. e. OBB normals)
    {
      for (var i: uint = 0; i < 3; ++i)
      {
        let m = axes[i];
        let mc = dot(m, obb.center);
        let rad = extents[i];
        let obbMin = mc - rad;
        let obbMax = mc + rad;

        let frustumInterval =
          project_frustum_onto_ax(xNear, yNear, zNear, zFar, m, view_params);

        if (obbMin > frustumInterval.y || obbMax < frustumInterval.x)
          return false;
      }
    }

    // Frustum X x OBB axes
    {
      for (var i: uint = 0; i < 3; ++i)
      {
        let m = float3(0.f, -axes[i].z, axes[i].y);

        let obbInterval = project_obb_onto_ax(
          obb.center, obb.xAxis, obb.yAxis, obb.zAxis, obb.extents, m);
        let frustumInterval = project_frustum_onto_ax(
          xNear, yNear, zNear, zFar, m, view_params);

        if (obbInterval.x > frustumInterval.y || obbInterval.y < frustumInterval.x)
          return false;
      }
    }

    // Frustum Y x OBB axes
    {
      for (var i: uint = 0; i < 3; ++i)
      {
        let m = float3(axes[i].z, 0.f, -axes[i].x);

        let obbInterval = project_obb_onto_ax(
          obb.center, obb.xAxis, obb.yAxis, obb.zAxis, obb.extents, m);
        let frustumInterval = project_frustum_onto_ax(
          xNear, yNear, zNear, zFar, m, view_params);

        if (obbInterval.x > frustumInterval.y || obbInterval.y < frustumInterval.x)
          return false;
      }
    }

    // Frustum Edges x OBB axes
    if (view_params.type == ViewType::PERSPECTIVE)
    {
      for (var i: uint = 0; i < 3; ++i)
      {
        let ms: float3[4] = {
          cross(float3(xNear, 0.f, zNear), axes[i]),
          cross(float3(-xNear, 0.f, zNear), axes[i]),
          cross(float3(0.f, yNear, zNear), axes[i]),
          cross(float3(0.f, -yNear, zNear), axes[i])};

        for (var j: uint = 0; j < 4; ++j)
        {
          let obbInterval = project_obb_onto_ax(
              obb.center, obb.xAxis, obb.yAxis, obb.zAxis, obb.extents, ms[j]);
          let frustumInterval = project_frustum_onto_ax(
              xNear, yNear, zNear, zFar, ms[j], view_params);

          if (obbInterval.x > frustumInterval.y || obbInterval.y < frustumInterval.x)
            return false;
        }
      }
    }
    else // ORTHO
    {
      for (var i: uint = 0; i < 3; ++i)
      {
        let m = cross(float3(0.f, 0.f, 1.f), axes[i]);

        let obbInterval = project_obb_onto_ax(
          obb.center, obb.xAxis, obb.yAxis, obb.zAxis, obb.extents, m);
        let frustumInterval = project_frustum_onto_ax(
          xNear, yNear, zNear, zFar, m, view_params);
      }
    }

    return true;
  }
  else
  {
    return false;
  }
}
