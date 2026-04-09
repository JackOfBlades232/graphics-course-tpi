#pragma once

#include <geometry.h>
#include <lights.h>

#include <glm/glm.hpp>
#include <glm/ext.hpp>


// @TODO: common code extr
//
struct Camera
{
  glm::vec3 position;
  glm::quat rotation;
  float fov{60};
  float zNear{0.01f};
  float zFar{2560.f};

  void lookAt(glm::vec3 from, glm::vec3 to, glm::vec3 up)
  {
    position = from;
    rotation = glm::quatLookAtLH(normalize(to - from), normalize(up));
  }

  void rotate(float up_angle_deg, float right_angle_deg)
  {
    glm::quat yaw = glm::angleAxis(glm::radians(right_angle_deg), glm::vec3{0, -1, 0});
    glm::quat pitch = glm::angleAxis(glm::radians(up_angle_deg), glm::vec3{1, 0, 0});
    rotation = yaw * rotation * pitch;
  }

  void move(glm::vec3 offset) { position += offset; }

  const glm::vec3 right() const { return rotation * glm::vec3{-1, 0, 0}; }

  const glm::vec3 up() const { return rotation * glm::vec3{0, 1, 0}; }

  const glm::vec3 forward() const { return rotation * glm::vec3{0, 0, 1}; }

  glm::mat4x4 viewItm() const
  {
    return translate(glm::identity<glm::mat4>(), position) * mat4_cast(rotation);
  }

  glm::mat4x4 viewTm() const { return inverse(viewItm()); }

  glm::mat4x4 projTm(float aspect) const
  {
    return glm::perspectiveLH_ZO(-glm::radians(fov), aspect, zNear, zFar);
  }
};

struct OrthoCamera
{
  glm::vec3 position;
  glm::quat rotation;
  float zNear{0.01f};
  float zFar{2560.f};

  void lookAt(glm::vec3 from, glm::vec3 to, glm::vec3 up)
  {
    position = from;
    rotation = glm::quatLookAtLH(normalize(to - from), normalize(up));
  }

  void rotate(float up_angle_deg, float right_angle_deg)
  {
    glm::quat yaw = glm::angleAxis(glm::radians(right_angle_deg), glm::vec3{0, -1, 0});
    glm::quat pitch = glm::angleAxis(glm::radians(up_angle_deg), glm::vec3{1, 0, 0});
    rotation = yaw * rotation * pitch;
  }

  void move(glm::vec3 offset) { position += offset; }

  const glm::vec3 right() const { return rotation * glm::vec3{-1, 0, 0}; }

  const glm::vec3 up() const { return rotation * glm::vec3{0, 1, 0}; }

  const glm::vec3 forward() const { return rotation * glm::vec3{0, 0, 1}; }

  glm::mat4x4 viewItm() const
  {
    return translate(glm::identity<glm::mat4>(), position) * mat4_cast(rotation);
  }

  glm::mat4x4 viewTm() const { return inverse(viewItm()); }

  glm::mat4x4 orthoTm(float xext, float yext) const
  {
    // @TODO: check top-bottom ori
    return glm::orthoLH_ZO(xext, -xext, yext, -yext, zNear, zFar);
  }
};

inline ViewParams view_params_for_cam(
  const Camera& cam,
  float aspect,
  bool need_depth_bounds,
  bool need_reverse_z,
  float csm_split_lambda = -1.f,
  float csm_shadow_dist = FLT_MAX)
{
  ViewParams params{};
  params.type = ViewType::PERSPECTIVE;

  Camera pcam = cam;
  if (need_reverse_z)
    std::swap(pcam.zNear, pcam.zFar);

  // calc camera matrix
  {
    const auto proj = pcam.projTm(aspect);
    params.mView = cam.viewTm();
    params.mProjView = proj * params.mView;
    params.mInverseView = glm::inverse(params.mView);
    params.mProj = proj;
    params.mViewPos =
      glm::vec3(params.mInverseView[3][0], params.mInverseView[3][1], params.mInverseView[3][2]) /
      params.mInverseView[3][3];
  }

  // pass frustum dimensions
  {
    params.viewFrustum.nearY = glm::tan(glm::radians(cam.fov * 0.5f)) * cam.zNear;
    params.viewFrustum.nearX = aspect * params.viewFrustum.nearY;
    params.viewFrustum.nearZ = cam.zNear;
    params.viewFrustum.farZ = cam.zFar;
  }

  // Frustum split for cascade maps
  if (csm_split_lambda >= 0.f && csm_split_lambda <= 1.f)
  {
    float* splits = reinterpret_cast<float*>(params.csmFrustumSplits);
    const float zfar =
      glm::clamp(csm_shadow_dist, params.viewFrustum.nearZ, params.viewFrustum.farZ);

    for (int i = 0; i < CSM_CASCADE_COUNT; ++i)
    {
      const int splitIdx = i + 1;
      const float cUni = params.viewFrustum.nearZ +
        (zfar - params.viewFrustum.nearZ) * float(splitIdx) / float(CSM_CASCADE_COUNT);
      const float cLog = params.viewFrustum.nearZ *
        glm::pow(zfar / params.viewFrustum.nearZ, float(splitIdx) / float(CSM_CASCADE_COUNT));
      splits[i] = csm_split_lambda * cLog + (1.f - csm_split_lambda) * cUni;
    }
  }

  params.needDepthBounds = shader_uint(need_depth_bounds);
  params.needReverseZ = shader_uint(need_reverse_z);

  return params;
}

inline ViewParams view_params_for_cam(
  const OrthoCamera& cam, float xext, float yext, bool need_depth_bounds)
{
  ViewParams params{};
  params.type = ViewType::ORTHO;

  // calc camera matrix
  {
    const auto proj = cam.orthoTm(xext, yext);
    params.mView = cam.viewTm();
    params.mProjView = proj * params.mView;
    params.mInverseView = glm::inverse(params.mView);
    params.mProj = proj;
    params.mViewPos =
      glm::vec3(params.mInverseView[3][0], params.mInverseView[3][1], params.mInverseView[3][2]) /
      params.mInverseView[3][3];
  }

  // pass frustum dimensions
  {
    params.viewFrustum.nearX = xext;
    params.viewFrustum.nearY = yext;
    params.viewFrustum.nearZ = cam.zNear;
    params.viewFrustum.farZ = cam.zFar;
  }

  params.needDepthBounds = shader_uint(need_depth_bounds);
  params.needReverseZ = 0u;

  return params;
}
