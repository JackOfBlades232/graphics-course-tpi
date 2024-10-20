#include "App.hpp"
#include "GlobalSettings.hpp"

#include <etna/Assert.hpp>

#include <cstring>

GlobalSettings gSettings{};

int main(int argc, char **argv)
{
  for (int i = 1; i < argc; ++i)
  {
    if (strncmp(argv[i], "-quantizedScene", 15) == 0)
      gSettings.useQuantizedScene = true;
    else
      ETNA_PANIC("Invalid arg");
  }

  {
    App app;
    app.run();
  }

  // Etna needs to be de-initialized after all resources allocated by app
  // and it's sub-fields are already freed.
  if (etna::is_initilized())
    etna::shutdown();

  return 0;
}
