#pragma once

#include "wsi/OsWindowingManager.hpp"
#include "scene/Camera.hpp"

#include "Renderer.hpp"
#include "Config.hpp"

#include <span>


class App
{
public:
  App(const char* scene_name, std::span<const char* const> argv);

  void run();

private:
  void processInput(float dt);
  void drawFrame();

  void moveCam(Camera& cam, const Keyboard& kb, float dt);
  void rotateCam(Camera& cam, const Mouse& ms, float dt);

  bool parseArgs(std::span<const char* const> argv);

private:
  OsWindowingManager windowing;
  std::unique_ptr<OsWindow> mainWindow;

  // @TODO: figure out how to remove #define renderer 1
  std::unique_ptr<Renderer> render;

  Config cfg;

  float camMoveSpeed = 1;
  float camRotateSpeed = 0.1f;
  float zoomSensitivity = 2.0f;
  Camera mainCam;
};
