#include <ezgl/application.hpp>

#include <iostream>

void mouse_click(GdkEventButton *event)
{
  if(event->type == GDK_BUTTON_PRESS) {
    std::cout << "User clicked mouse at " << event->x << ", " << event->y << "\n";
  } else if(event->type == GDK_BUTTON_RELEASE) {
    std::cout << "User released mouse button at " << event->x << ", " << event->y << "\n";
  }
}

int main(int argc, char **argv)
{
  // ezgl applications require a settings object
  ezgl::settings settings;
  // customize the string to display in the title of the window
  settings.window.title = "Input Mouse Click Example";
  // specify the callback to use when a mouse is clicked
  settings.input.mouse_click_callback = mouse_click;

  // create the application based on the above settings
  ezgl::application application(settings);

  // run the application until the user quits
  return application.run(argc, argv);
}