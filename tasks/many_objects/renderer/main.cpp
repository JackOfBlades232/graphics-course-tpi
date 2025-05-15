#include "App.hpp"

int main(int argc, char **argv)
{
  if (argc < 2)
  {
    spdlog::error("Invalid args, usage: prog [scene name]");
    return 1;
  }

  {
    App app{argv[1]};
    app.run();
  }

  // Etna needs to be de-initialized after all resources allocated by app
  // and it's sub-fields are already freed.
  if (etna::is_initilized())
    etna::shutdown();

  return 0;
}
