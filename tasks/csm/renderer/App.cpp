#include "App.hpp"

#include <tracy/Tracy.hpp>

#include <filesystem>
#include <string>
#include <charconv>


// @TODO: pull out
template <class TS>
  requires(std::same_as<TS, std::string> || std::same_as<TS, std::wstring>)
std::string to_char_str(const TS& s)
{
  if constexpr (std::same_as<TS, std::string>)
    return s;
  else
    return std::to_string(s);
}

App::App(const char* scene_name, std::span<const char* const> argv)
{
  if (!parseArgs(argv))
    ETNA_PANIC("Invalid args");

  glm::uvec2 initialRes = {1280, 720};
  mainWindow = windowing.createWindow(OsWindow::CreateInfo{
    .resolution = initialRes,
    .resizeable = true,
    // @TODO: provide some refresh capability while resizing, and check if we need spec on-resize
    .name = std::string{NAME}});

  render.reset(new Renderer{initialRes, cfg});

  auto instExts = windowing.getRequiredVulkanInstanceExtensions();
  render->initVulkan(NAME.data(), instExts);

  auto surface = mainWindow->createVkSurface(etna::get_context().getInstance());

  render->initFrameDelivery(std::move(surface), [this]() { return mainWindow->getResolution(); });

  ImGuiRenderer::enableImGuiForWindow(mainWindow->native());

  mainCam.lookAt({0, 10, 10}, {0, 0, 0}, {0, 1, 0});

  // @TODO: how is this validated?
  std::filesystem::path scenePath{
    (GRAPHICS_COURSE_RESOURCES_ROOT "/scenes/") + std::string{scene_name} + "/baked/"};
  for (const auto& entry : std::filesystem::directory_iterator(scenePath))
  {
    // @TODO: do I have to support binary file separately? Don't remember
    if (entry.is_regular_file() && entry.path().extension() == ".gltf")
    {
      spdlog::info("Loading scene from {}", to_char_str(entry.path().string()));
      render->loadScene(entry.path().c_str());
      return;
    }
  }

  ETNA_PANIC("A .gltf file was not found in {}", to_char_str(scenePath.string()));
}

void App::run()
{
  double lastTime = windowing.getTime();
  while (!mainWindow->isBeingClosed())
  {
    const double currTime = windowing.getTime();
    const float diffTime = static_cast<float>(currTime - lastTime);
    lastTime = currTime;

    windowing.poll();

    processInput(diffTime);

    drawFrame();

    FrameMark;
  }
}

void App::processInput(float dt)
{
  ZoneScoped;

  if (mainWindow->keyboard[KeyboardKey::kEscape] == ButtonState::Falling)
    mainWindow->askToClose();

  if (is_held_down(mainWindow->keyboard[KeyboardKey::kLeftShift]))
    camMoveSpeed = 10;
  else
    camMoveSpeed = 1;

  if (mainWindow->mouse[MouseButton::mbRight] == ButtonState::Rising)
    mainWindow->captureMouse = !mainWindow->captureMouse;

  moveCam(mainCam, mainWindow->keyboard, dt);
  if (mainWindow->captureMouse)
    rotateCam(mainCam, mainWindow->mouse, dt);

  render->debugInput(mainWindow->keyboard, mainWindow->mouse, mainWindow->captureMouse);
}

void App::drawFrame()
{
  ZoneScoped;

  render->update(FramePacket{
    .mainCam = mainCam,
    .currentTime = static_cast<float>(windowing.getTime()),
  });
  render->drawFrame();
}

void App::moveCam(Camera& cam, const Keyboard& kb, float dt)
{
  // Move position of camera based on WASD keys, and FR keys for up and down

  glm::vec3 dir = {0, 0, 0};

  if (is_held_down(kb[KeyboardKey::kS]))
    dir -= cam.forward();

  if (is_held_down(kb[KeyboardKey::kW]))
    dir += cam.forward();

  if (is_held_down(kb[KeyboardKey::kA]))
    dir -= cam.right();

  if (is_held_down(kb[KeyboardKey::kD]))
    dir += cam.right();

  if (is_held_down(kb[KeyboardKey::kF]))
    dir -= cam.up();

  if (is_held_down(kb[KeyboardKey::kR]))
    dir += cam.up();

  // NOTE: This is how you make moving diagonally not be faster than
  // in a straight line.
  cam.move(dt * camMoveSpeed * (length(dir) > 1e-9 ? normalize(dir) : dir));
}

void App::rotateCam(Camera& cam, const Mouse& ms, float /*dt*/)
{
  // Rotate camera based on mouse movement
  cam.rotate(camRotateSpeed * ms.capturedPosDelta.y, camRotateSpeed * ms.capturedPosDelta.x);

  // Increase or decrease field of view based on mouse wheel
  cam.fov -= zoomSensitivity * ms.scrollDelta.y;
  if (cam.fov < 1.0f)
    cam.fov = 1.0f;
  if (cam.fov > 120.0f)
    cam.fov = 120.0f;
}

bool App::parseArgs(std::span<const char* const> argv)
{
  bool hasDebugConfigOverride = false;

  for (auto it = argv.begin(); it < argv.end(); ++it)
  {
    const std::string_view arg{*it};

    if (arg == "-multiplexScene")
    {
      auto err = [] {
        spdlog::error("Wrong -multiplexScene usage, correct: -multiplexScene "
                      "[X](uint > 0) [Y](uint > 0) [Z](uint > 0) "
                      "[X offset](float > 0) [Y offset](float > 0) [Z offset](float > 0)");
      };
      cfg.testMultiplexScene = true;
      ++it;
      int id = 0;
      for (auto end = it + 6; it < end; ++it, ++id)
      {
        if (it == argv.end())
        {
          err();
          return false;
        }

        const std::string_view valarg{*it};
        const bool isDim = id < 3;
        const size_t component = id % 3;

        if (isDim)
        {
          unsigned dest = 0u;
          auto [ptr, ec] = std::from_chars(valarg.data(), valarg.data() + valarg.size(), dest);
          if (ec != std::errc{} || size_t(ptr - valarg.data()) != valarg.size() || dest == 0)
          {
            err();
            return false;
          }

          ((glm::uint*)&cfg.testMultiplexing.dims)[component] = dest;
        }
        else
        {
          float dest = 0.f;
          auto [ptr, ec] = std::from_chars(valarg.data(), valarg.data() + valarg.size(), dest);
          if (
            ec != std::errc{} || size_t(ptr - valarg.data()) != valarg.size() ||
            dest <= BIG_EPSILON)
          {
            err();
            return false;
          }

          ((float*)&cfg.testMultiplexing.offsets)[component] = dest;
        }
      }
    }
    else if (arg == "-noDebugConfig")
    {
      cfg.useDebugConfig = false;
    }
    else if (arg == "-debugConfigFile")
    {
      auto err = [] {
        spdlog::error("Wrong -debugConfigFile usage, correct: -debugConfigFile [filepath]");
      };

      ++it;
      if (it == argv.end())
      {
        err();
        return false;
      }

      hasDebugConfigOverride = true;

      const std::string_view valarg{*it};
      cfg.debugConfigFile = valarg;
    }
    else
    {
      spdlog::error("Unknown argument {}", arg);
      return false;
    }
  }

  if (cfg.testMultiplexScene)
  {
    spdlog::info(
      "Scene multiplexing on: dims=<{}, {}, {}>, step=<{}, {}, {}>",
      cfg.testMultiplexing.dims.x,
      cfg.testMultiplexing.dims.y,
      cfg.testMultiplexing.dims.z,
      cfg.testMultiplexing.offsets.x,
      cfg.testMultiplexing.offsets.y,
      cfg.testMultiplexing.offsets.z);
  }

  if (hasDebugConfigOverride && !cfg.useDebugConfig)
  {
    spdlog::error("-noDebugConfig and -debugConfigFile flags are incompatible");
    return false;
  }

  return true;
}
