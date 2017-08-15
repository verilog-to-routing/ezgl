#include <ezgl/application.hpp>

int main(int argc, char ** argv)
{
  ezgl::application application = ezgl::create_application(argc, argv, "Basic Application");

  application.run();

  return 0;
}