#include "App.hpp"
#include <cstdio>
#include <filesystem>

int main(int argc, char **argv)
{
  if (argc < 2)
  {
    printf("Usage: GLLSimulator <program.gll>\n");
    printf("       The file is created if it does not exist.\n");
    return 1;
  }

  std::error_code ec;
  std::filesystem::path exe = std::filesystem::absolute(argv[0], ec);
  App app(argv[1], exe.parent_path());
  return app.run();
}
