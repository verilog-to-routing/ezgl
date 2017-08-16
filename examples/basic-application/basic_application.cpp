#include <ezgl/application.hpp>

void my_draw(cairo_t *cairo, int width, int height)
{
  // draw a circle around the screen
  cairo_arc(cairo, width / 2.0, height / 2.0, MIN(width, height) / 2.0, 0, 2 * G_PI);
}

int main(int argc, char **argv)
{
  // ezgl applications require a settings object
  ezgl::settings settings;
  // customize the string to display in the title of the window
  settings.window.title = "Basic Application Example";
  // specify the callback to use to draw in the window
  settings.graphics.draw_callback = my_draw;

  ezgl::application application(settings);

  return application.run(argc, argv);
}