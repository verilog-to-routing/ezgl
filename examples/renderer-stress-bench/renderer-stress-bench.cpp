/*
 * Copyright 2019-2023 University of Toronto
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Authors: Mario Badr, Sameh Attia, Tanner Young-Schultz and Vaughn Betz
 */

#include <iostream>
#include <chrono>
#include "ezgl/application.hpp"
#include "ezgl/graphics.hpp"

// Layout: 100 cols x 100 rows = 10 000 cells, each 4x4 world units → 1000x1000 image.
static constexpr int    N    = 10000;
static constexpr int    COLS = 100;
static constexpr double CELL = 10.0;
static constexpr int    IMG_W = static_cast<int>(COLS * CELL);
static constexpr int    IMG_H = static_cast<int>(COLS * CELL);
static const ezgl::rectangle WORLD{{0, 0}, (double)IMG_W, (double)IMG_H};

// ---- draw callbacks -------------------------------------------------------

void draw_lines_solid(ezgl::renderer *g)
{
  g->set_color(ezgl::BLUE);
  g->set_line_width(1);
  g->set_line_dash(ezgl::line_dash::none);
  for (int i = 0; i < N; ++i) {
    double x0 = (i % COLS) * CELL + 4;
    double y0 = (i / COLS) * CELL + 4;
    g->draw_line({x0, y0}, {x0 + CELL - 8, y0 + CELL - 8});
  }
}

void draw_lines_transparent(ezgl::renderer *g)
{
  g->set_color(ezgl::BLUE, 128);
  g->set_line_width(1);
  g->set_line_dash(ezgl::line_dash::none);
  for (int i = 0; i < N; ++i) {
    double x0 = (i % COLS) * CELL + 4;
    double y0 = (i / COLS) * CELL + 4;
    g->draw_line({x0, y0}, {x0 + CELL - 8, y0 + CELL - 8});
  }
}

void draw_rectangles_solid(ezgl::renderer *g)
{
  g->set_color(ezgl::RED);
  for (int i = 0; i < N; ++i) {
    double x = (i % COLS) * CELL + 4;
    double y = (i / COLS) * CELL + 4;
    g->fill_rectangle({x, y}, {x + CELL - 8, y + CELL - 8});
  }
}

void draw_rectangles_transparent(ezgl::renderer *g)
{
  g->set_color(ezgl::RED, 128);
  for (int i = 0; i < N; ++i) {
    double x = (i % COLS) * CELL + 4;
    double y = (i / COLS) * CELL + 4;
    g->fill_rectangle({x, y}, {x + CELL - 8, y + CELL - 8});
  }
}

void draw_chars(ezgl::renderer *g)
{
  g->set_color(ezgl::BLACK);
  g->set_font_size(10);
  for (int i = 0; i < N; ++i) {
    double cx = (i % COLS) * CELL + CELL / 2.0;
    double cy = (i / COLS) * CELL + CELL / 2.0;
    char ch[2] = {static_cast<char>('A' + (i % 26)), '\0'};
    g->draw_text({cx, cy}, ch, CELL, CELL);
  }
}

// ---- bench helper ---------------------------------------------------------

static void run_bench(ezgl::application &app,
                      const char *label,
                      ezgl::draw_canvas_fn fn,
                      const char *output_file,
                      const char *canvas_id)
{
  app.add_canvas(canvas_id, fn, WORLD, ezgl::WHITE);
  ezgl::canvas *c = app.get_canvas(canvas_id);

  auto t0 = std::chrono::high_resolution_clock::now();
  c->print_png(output_file, IMG_W, IMG_H);
  auto t1 = std::chrono::high_resolution_clock::now();

  double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
  std::cout << label << "(" << N << "): " << ms << " ms  ->  " << output_file << "\n";
}

// ---------------------------------------------------------------------------

int main(int argc, char **argv)
{
  ezgl::application::settings s;
  // We never call run(), so main_ui_resource is never loaded.
  // canvas::print_png() with explicit dimensions is self-contained.
#ifdef EZGL_QT
  ezgl::application app(s, argc, argv);
#else
  ezgl::application app(s);
#endif

  run_bench(app, "lines solid       ", draw_lines_solid,        "bench_lines_solid.png",        "bench_lines_solid");
  run_bench(app, "lines transparent ", draw_lines_transparent,  "bench_lines_transparent.png",  "bench_lines_transparent");
  run_bench(app, "rects solid       ", draw_rectangles_solid,   "bench_rects_solid.png",        "bench_rects_solid");
  run_bench(app, "rects transparent ", draw_rectangles_transparent, "bench_rects_transparent.png", "bench_rects_transparent");
  run_bench(app, "chars             ", draw_chars,              "bench_chars.png",              "bench_chars");

  return 0;
}
