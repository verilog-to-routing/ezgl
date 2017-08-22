#include <ezgl/application.hpp>

void draw_screen(ezgl::graphics g, int width, int height)
{
  // draws a blue line through the drawable area
  g.set_colour(ezgl::BLUE);
  g.draw_line({0, 0}, {width, height});

  // change the next draw calls to use the colour red
  g.set_colour(ezgl::RED);

  // draw rectangle outlines...
  g.draw_rectangle({100, 100}, {400, 300}); // from one point to another
  g.draw_rectangle({10, 10}, 50, 50);       // from one point with a width and height

  g.draw_text({100,100}, "Hello World!");

  // change the next draw calls to use green with half transparency
  g.set_colour(ezgl::colour{0, 1, 0, 0.5});

  // draw filled in rectangles...
  g.fill_rectangle({500, 50}, {600, 300}); // from one point to another
  g.fill_rectangle({500, 50}, 50, 50);     // from one point with a width and height
}

int main(int argc, char **argv)
{
  // ezgl applications require a settings object
  ezgl::settings settings;
  // customize the string to display in the title of the window
  settings.window.title = "Basic Application Example";
  // specify the background colour of the drawable area
  settings.graphics.background = ezgl::BLACK;
  // specify the callback to use to draw in the window
  settings.graphics.draw_callback = draw_screen;

  // create the application based on the above settings
  ezgl::application application(settings);

  // run the application until the user quits
  return application.run(argc, argv);
}