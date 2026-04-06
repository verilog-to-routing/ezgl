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

/**
 * Usage:
 *   renderer-stress-bench           — headless: run all benchmarks, print timing, save PNGs
 *   renderer-stress-bench --ui      — UI: open window showing first test case
 *   renderer-stress-bench --ui <n>  — UI: open window showing test case n (0-based)
 *   renderer-stress-bench --help    — show this help
 */

#include <iostream>
#include <fstream>
#include <map>
#include <chrono>
#include <string>
#include <iomanip>
#include <sstream>
#include "ezgl/application.hpp"
#include "ezgl/graphics.hpp"

static const char *RESULTS_FILE = "renderer-stress-bench-results.txt";

// Write (or overwrite) a key=iterations entry in the results file.
static void write_result(const std::string &key, int iterations)
{
  std::map<std::string, int> results;
  {
    std::ifstream in(RESULTS_FILE);
    std::string line;
    while (std::getline(in, line)) {
      auto eq = line.find('=');
      if (eq != std::string::npos)
        results[line.substr(0, eq)] = std::stoi(line.substr(eq + 1));
    }
  }
  results[key] = iterations;
  std::ofstream out(RESULTS_FILE);
  for (const auto &kv : results)
    out << kv.first << "=" << kv.second << "\n";
}

// Layout: 1000 cols x 1000 rows = 1 000 000 cells, each 1x1 world unit → 1000x1000 image.
static constexpr int    N    = 1000000;
static constexpr int    COLS = 1000;
static constexpr double CELL = 1.0;
static constexpr double PAD  = CELL * 0.2;   // inset for lines/rects within each cell
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
    double x0 = (i % COLS) * CELL + PAD;
    double y0 = (i / COLS) * CELL + PAD;
    g->draw_line({x0, y0}, {x0 + CELL - 2 * PAD, y0 + CELL - 2 * PAD});
  }
}

void draw_lines_transparent(ezgl::renderer *g)
{
  g->set_color(ezgl::BLUE, 128);
  g->set_line_width(1);
  g->set_line_dash(ezgl::line_dash::none);
  for (int i = 0; i < N; ++i) {
    double x0 = (i % COLS) * CELL + PAD;
    double y0 = (i / COLS) * CELL + PAD;
    g->draw_line({x0, y0}, {x0 + CELL - 2 * PAD, y0 + CELL - 2 * PAD});
  }
}

void draw_rectangles_solid(ezgl::renderer *g)
{
  g->set_color(ezgl::RED);
  for (int i = 0; i < N; ++i) {
    double x = (i % COLS) * CELL + PAD;
    double y = (i / COLS) * CELL + PAD;
    g->fill_rectangle({x, y}, {x + CELL - 2 * PAD, y + CELL - 2 * PAD});
  }
}

void draw_rectangles_transparent(ezgl::renderer *g)
{
  g->set_color(ezgl::RED, 128);
  for (int i = 0; i < N; ++i) {
    double x = (i % COLS) * CELL + PAD;
    double y = (i / COLS) * CELL + PAD;
    g->fill_rectangle({x, y}, {x + CELL - 2 * PAD, y + CELL - 2 * PAD});
  }
}

void draw_chars(ezgl::renderer *g)
{
  g->set_color(ezgl::BLACK);
  g->set_font_size(std::max(1, static_cast<int>(CELL)));
  for (int i = 0; i < N; ++i) {
    double cx = (i % COLS) * CELL + CELL / 2.0;
    double cy = (i / COLS) * CELL + CELL / 2.0;
    char ch[2] = {static_cast<char>('A' + (i % 26)), '\0'};
    g->draw_text({cx, cy}, ch, CELL, CELL);
  }
}

// ---- test case table -------------------------------------------------------

struct TestCase {
  const char          *label;
  ezgl::draw_canvas_fn fn;
  const char          *canvas_id;
  const char          *output_file;
};

static const TestCase TESTS[] = {
  { "lines solid      ", draw_lines_solid,           "bench_lines_solid",       "bench_lines_solid.png"       },
  { "lines transparen ", draw_lines_transparent,     "bench_lines_transparent", "bench_lines_transparent.png" },
  { "rects solid      ", draw_rectangles_solid,      "bench_rects_solid",       "bench_rects_solid.png"       },
  { "rects transparen ", draw_rectangles_transparent,"bench_rects_transparent", "bench_rects_transparent.png" },
  //{ "chars            ", draw_chars,                 "bench_chars",             "bench_chars.png"             },
};
static constexpr int N_TESTS = static_cast<int>(sizeof(TESTS) / sizeof(TESTS[0]));

// ---- headless mode ---------------------------------------------------------

static void run_headless()
{
  ezgl::application::settings s;
#ifdef EZGL_QT
  s.main_ui_resource = ":/main.ui";
#else
  s.main_ui_resource = "/ezgl/main.ui";
#endif

#ifdef EZGL_QT
  static int   fake_argc    = 1;
  static char  fake_argv0[] = "renderer-stress-bench";
  static char *fake_argv[]  = {fake_argv0, nullptr};
  ezgl::application app(s, fake_argc, fake_argv);
#else
  ezgl::application app(s);
#endif

  for (int t = 0; t < N_TESTS; ++t) {
    const TestCase &tc = TESTS[t];
    app.add_canvas(tc.canvas_id, tc.fn, WORLD, ezgl::WHITE);
    ezgl::canvas *c = app.get_canvas(tc.canvas_id);

    auto t0 = std::chrono::high_resolution_clock::now();
    c->print_png(tc.output_file, IMG_W, IMG_H);
    auto t1 = std::chrono::high_resolution_clock::now();

    double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    std::cout << tc.label << "(" << N << "): " << ms << " ms  ->  " << tc.output_file << "\n";

    std::string key = "headless:" + std::string(tc.label);
    key.erase(key.find_last_not_of(" \t") + 1);
    write_result(key, N);
  }
}

// ---- UI mode ---------------------------------------------------------------

static int                g_current_test  = 0;
static ezgl::application *g_app           = nullptr;
static double             g_last_frame_ms = -1.0;

static void draw_dispatch(ezgl::renderer *g)
{
  auto t0 = std::chrono::high_resolution_clock::now();
  TESTS[g_current_test].fn(g);
  auto t1 = std::chrono::high_resolution_clock::now();
  g_last_frame_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

  {
    std::string key = "ui:" + std::string(TESTS[g_current_test].label);
    key.erase(key.find_last_not_of(" \t") + 1);
    write_result(key, N);
  }

  if (g_app) {
    std::ostringstream oss;
    oss << TESTS[g_current_test].label
        << " | " << N << " primitives"
        << " | " << std::fixed << std::setprecision(2) << g_last_frame_ms << " ms";
    g_app->update_message(oss.str());
  }
}

static void switch_test(ezgl::application *app, int delta)
{
  g_current_test  = (g_current_test + delta + N_TESTS) % N_TESTS;
  app->change_canvas_world_coordinates("MainCanvas", WORLD);
  app->refresh_drawing();
}

static void ui_setup(ezgl::application *app, bool /*new_window*/)
{
  g_app = app;

  app->create_button("Prev", 6, [](GtkWidget *, ezgl::application *a) {
    switch_test(a, -1);
  });
  app->create_button("Next", 7, [](GtkWidget *, ezgl::application *a) {
    switch_test(a, +1);
  });

  app->refresh_drawing();
}

static void run_ui(int initial_test)
{
  g_current_test = initial_test;

  ezgl::application::settings s;
#ifdef EZGL_QT
  s.main_ui_resource = ":/main.ui";
#else
  s.main_ui_resource = "/ezgl/main.ui";
#endif
  s.window_identifier = "MainWindow";
  s.canvas_identifier = "MainCanvas";

#ifdef EZGL_QT
  static int   fake_argc    = 1;
  static char  fake_argv0[] = "renderer-stress-bench";
  static char *fake_argv[]  = {fake_argv0, nullptr};
  ezgl::application app(s, fake_argc, fake_argv);
#else
  ezgl::application app(s);
#endif

  app.add_canvas("MainCanvas", draw_dispatch, WORLD, ezgl::WHITE);
  app.run(ui_setup, nullptr, nullptr, nullptr);
}

// ---- help / argument parsing -----------------------------------------------

static void print_help(const char *prog)
{
  std::cout <<
    "Usage:\n"
    "  " << prog << "              Run all benchmarks headless, print timing, save PNGs\n"
    "  " << prog << " --ui [N]    Open UI window showing test case N (default 0)\n"
    "\n"
    "Test cases (N):\n";
  for (int i = 0; i < N_TESTS; ++i)
    std::cout << "  " << i << "  " << TESTS[i].label << "\n";
  std::cout <<
    "\n"
    "Options:\n"
    "  --ui [N]   Open interactive window for test N\n"
    "  --help     Show this message\n";
}

int main(int argc, char **argv)
{
  bool ui_mode    = false;
  int  initial    = 0;
  bool got_index  = false;

  for (int i = 1; i < argc; ++i) {
    std::string arg(argv[i]);
    if (arg == "--help" || arg == "-h") {
      print_help(argv[0]);
      return 0;
    } else if (arg == "--ui") {
      ui_mode = true;
    } else if (ui_mode && !got_index) {
      try {
        int n = std::stoi(arg);
        if (n < 0 || n >= N_TESTS) {
          std::cerr << "Error: test index " << n
                    << " out of range [0," << N_TESTS - 1 << "]\n\n";
          print_help(argv[0]);
          return 1;
        }
        initial   = n;
        got_index = true;
      } catch (...) {
        std::cerr << "Error: unknown argument '" << arg << "'\n\n";
        print_help(argv[0]);
        return 1;
      }
    } else {
      std::cerr << "Error: unknown argument '" << arg << "'\n\n";
      print_help(argv[0]);
      return 1;
    }
  }

  if (ui_mode)
    run_ui(initial);
  else
    run_headless();

  return 0;
}
