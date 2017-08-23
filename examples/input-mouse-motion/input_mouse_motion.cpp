#include <ezgl/application.hpp>

#include <iostream>

void mouse_move(GdkEventMotion *event)
{
  std::cout << event->x << ", " << event->y << "\n";
}

int main(int argc, char **argv)
{
  // ezgl applications require a settings object
  ezgl::settings settings;
  // customize the string to display in the title of the window
  settings.window.title = "Input Mouse Motion Example";
  // enable the tracking of mouse movement
  settings.input.track_mouse_motion = true;
  // specify the callback to use when a mouse is moved
  settings.input.mouse_move_callback = mouse_move;

  // create the application based on the above settings
  ezgl::application application(settings);

  // run the application until the user quits
  return application.run(argc, argv);
}