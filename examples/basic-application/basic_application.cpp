#include <ezgl/application.hpp>

int main(int argc, char ** argv)
{
  ezgl::settings settings;
  settings.window.title = "Basic Application Example";

  ezgl::application application(settings);

  application.run(argc, argv);

  return 0;
}