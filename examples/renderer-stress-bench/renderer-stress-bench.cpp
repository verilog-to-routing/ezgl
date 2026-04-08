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
#include <algorithm>
#include <fstream>
#include <map>
#include <chrono>
#include <cmath>
#include <string>
#include <iomanip>
#include <sstream>
#include "ezgl/application.hpp"
#include "ezgl/graphics.hpp"

static const char *RESULTS_FILE = "renderer-stress-bench-results.txt";

// Write (or overwrite) an entry in the results file.
// Line format: "headless:1000000 solid lines   \t  957.25 ms"
// Keys are padded to a uniform width so the ms column aligns.
static void write_result(const std::string &key, double ms)
{
  std::map<std::string, double> results;
  {
    std::ifstream in(RESULTS_FILE);
    std::string line;
    while (std::getline(in, line)) {
      // format: "key<whitespace>142.37 ms"
      // locate number by scanning backwards from " ms"
      auto ms_pos = line.rfind(" ms");
      if (ms_pos == std::string::npos) continue;
      auto num_end = ms_pos;
      auto num_start = num_end;
      while (num_start > 0 && (std::isdigit(line[num_start - 1]) || line[num_start - 1] == '.'))
        --num_start;
      if (num_start == num_end) continue;
      std::string k = line.substr(0, num_start);
      k.erase(k.find_last_not_of(" \t") + 1);
      if (k.empty()) continue;
      results[k] = std::stod(line.substr(num_start, num_end - num_start));
    }
  }
  results[key] = ms;

  std::size_t col = 0;
  for (const auto &kv : results)
    col = std::max(col, kv.first.size());
  col += 2; // minimum gap between key and value

  std::ofstream out(RESULTS_FILE);
  out << std::fixed << std::setprecision(2);
  for (const auto &kv : results)
    out << std::left << std::setw(static_cast<int>(col)) << kv.first << kv.second << " ms\n";
}

// Current primitive count used by draw callbacks.
static int g_bench_n = 1000000;

// Fixed world/image size. Primitive placement adapts dynamically so any
// primitive count is packed inside this world rectangle.
static constexpr int    IMG_W = 1000;
static constexpr int    IMG_H = 1000;
static const ezgl::rectangle WORLD{{0, 0}, (double)IMG_W, (double)IMG_H};

struct GridLayout {
  int    cols;
  double cell_w;
  double cell_h;
  double pad_x;
  double pad_y;
};

struct PrimitivePlacement {
  double x0;
  double y0;
  double x1;
  double y1;
  double cx;
  double cy;
};

static GridLayout current_layout()
{
  const int n = std::max(1, g_bench_n);
  const double aspect = WORLD.width() / WORLD.height();
  const int cols = std::max(1, static_cast<int>(std::ceil(std::sqrt(double(n) * aspect))));
  const int rows = std::max(1, (n + cols - 1) / cols);
  const double cell_w = WORLD.width() / double(cols);
  const double cell_h = WORLD.height() / double(rows);
  return {cols, cell_w, cell_h, cell_w * 0.2, cell_h * 0.2};
}

static PrimitivePlacement placement_for(const GridLayout &layout, int i)
{
  const int col = i % layout.cols;
  const int row = i / layout.cols;
  const double cell_left = WORLD.left() + double(col) * layout.cell_w;
  const double cell_bottom = WORLD.bottom() + double(row) * layout.cell_h;
  const double cell_right = cell_left + layout.cell_w;
  const double cell_top = cell_bottom + layout.cell_h;

  return {
    cell_left + layout.pad_x,
    cell_bottom + layout.pad_y,
    cell_right - layout.pad_x,
    cell_top - layout.pad_y,
    cell_left + layout.cell_w * 0.5,
    cell_bottom + layout.cell_h * 0.5
  };
}

// ---- draw callbacks -------------------------------------------------------

void draw_lines_solid(ezgl::renderer *g)
{
  const GridLayout layout = current_layout();
  g->set_color(ezgl::BLUE);
  g->set_line_width(1);
  g->set_line_dash(ezgl::line_dash::none);
  for (int i = 0; i < g_bench_n; ++i) {
    const PrimitivePlacement p = placement_for(layout, i);
    g->draw_line({p.x0, p.y0}, {p.x1, p.y1});
  }
}

void draw_lines_transparent(ezgl::renderer *g)
{
  const GridLayout layout = current_layout();
  g->set_color(ezgl::BLUE, 128);
  g->set_line_width(1);
  g->set_line_dash(ezgl::line_dash::none);
  for (int i = 0; i < g_bench_n; ++i) {
    const PrimitivePlacement p = placement_for(layout, i);
    g->draw_line({p.x0, p.y0}, {p.x1, p.y1});
  }
}

void draw_rectangles_solid(ezgl::renderer *g)
{
  const GridLayout layout = current_layout();
  g->set_color(ezgl::RED);
  for (int i = 0; i < g_bench_n; ++i) {
    const PrimitivePlacement p = placement_for(layout, i);
    g->fill_rectangle({p.x0, p.y0}, {p.x1, p.y1});
  }
}

void draw_rectangles_transparent(ezgl::renderer *g)
{
  const GridLayout layout = current_layout();
  g->set_color(ezgl::RED, 128);
  for (int i = 0; i < g_bench_n; ++i) {
    const PrimitivePlacement p = placement_for(layout, i);
    g->fill_rectangle({p.x0, p.y0}, {p.x1, p.y1});
  }
}

// ---- variadic-style palettes (deterministic, index-based) ----------------

struct LineStyle {
  ezgl::color  color;
  uint_fast8_t alpha;
  int          width;
  ezgl::line_dash dash;
};

struct RectStyle {
  ezgl::color  color;
  uint_fast8_t alpha;
};

static const LineStyle LINE_PALETTE[] = {
  { ezgl::BLUE,   255, 1, ezgl::line_dash::none },
  { ezgl::RED,    255, 2, ezgl::line_dash::asymmetric_5_3 },
  { ezgl::GREEN,  255, 3, ezgl::line_dash::none },
  { ezgl::ORANGE, 255, 4, ezgl::line_dash::asymmetric_5_3 },
  { ezgl::BLUE,   128, 1, ezgl::line_dash::none },
  { ezgl::RED,    128, 2, ezgl::line_dash::asymmetric_5_3 },
  { ezgl::GREEN,  128, 3, ezgl::line_dash::none },
  { ezgl::ORANGE, 128, 4, ezgl::line_dash::asymmetric_5_3 },
};
static constexpr int LINE_PALETTE_SIZE = static_cast<int>(sizeof(LINE_PALETTE) / sizeof(LINE_PALETTE[0]));

static const RectStyle RECT_PALETTE[] = {
  { ezgl::BLUE,  255 },
  { ezgl::RED,   255 },
  { ezgl::GREEN, 255 },
  { ezgl::CYAN,  255 },
  { ezgl::BLUE,  128 },
  { ezgl::RED,   128 },
  { ezgl::GREEN, 128 },
  { ezgl::CYAN,  128 },
};
static constexpr int RECT_PALETTE_SIZE = static_cast<int>(sizeof(RECT_PALETTE) / sizeof(RECT_PALETTE[0]));

void draw_solid(ezgl::renderer *g)
{
  const int num = g_bench_n/3;

  const GridLayout layout = current_layout();
  // fill rects
  g->set_color(ezgl::RED);
  for (int i = 0; i < num; ++i) {
    const PrimitivePlacement p = placement_for(layout, i);
    g->fill_rectangle({p.x0, p.y0}, {p.x1, p.y1});
  }

  // draw rects
  g->set_color(ezgl::GREEN);
  g->set_line_width(0);
  g->set_line_dash(ezgl::line_dash::none);
  for (int i = 0; i < num; ++i) {
    const PrimitivePlacement p = placement_for(layout, i);
    g->draw_rectangle({p.x0 + 10, p.y0}, {p.x1, p.y1});
  }

  // lines
  g->set_color(ezgl::BLUE);
  g->set_line_width(0);
  g->set_line_dash(ezgl::line_dash::none);
  for (int i = 0; i < num; ++i) {
    const PrimitivePlacement p = placement_for(layout, i);
    g->draw_line({p.x0, p.y0}, {p.x1, p.y1});
  }
}

void draw_variadic(ezgl::renderer *g)
{
  const int num = g_bench_n/3;

  const GridLayout layout = current_layout();
  // fill rects
  for (int i = 0; i < num/2; ++i) {
    const RectStyle &s = RECT_PALETTE[i % RECT_PALETTE_SIZE];
    g->set_color(s.color, s.alpha);
    const PrimitivePlacement p = placement_for(layout, i);
    g->fill_rectangle({p.x0, p.y0}, {p.x1, p.y1});
  }

  // draw rects
  g->set_line_width(0);
  g->set_line_dash(ezgl::line_dash::none);
  for (int i = 0; i < num; ++i) {
    const RectStyle &s = RECT_PALETTE[i % RECT_PALETTE_SIZE];
    g->set_color(s.color, s.alpha);
    const PrimitivePlacement p = placement_for(layout, i);
    g->draw_rectangle({p.x0 + 10, p.y0}, {p.x1, p.y1});
  }

  // lines
  for (int i = 0; i < num; ++i) {
    const LineStyle &s = LINE_PALETTE[i % LINE_PALETTE_SIZE];
    g->set_color(s.color, s.alpha);
    g->set_line_width(s.width);
    g->set_line_dash(s.dash);
    const PrimitivePlacement p = placement_for(layout, i);
    g->draw_line({p.x0, p.y0}, {p.x1, p.y1});
  }
}

void draw_lines_variadic(ezgl::renderer *g)
{
  const GridLayout layout = current_layout();
  for (int i = 0; i < g_bench_n; ++i) {
    const LineStyle &s = LINE_PALETTE[i % LINE_PALETTE_SIZE];
    g->set_color(s.color, s.alpha);
    g->set_line_width(s.width);
    g->set_line_dash(s.dash);
    const PrimitivePlacement p = placement_for(layout, i);
    g->draw_line({p.x0, p.y0}, {p.x1, p.y1});
  }
}

void draw_rectangles_variadic(ezgl::renderer *g)
{
  const GridLayout layout = current_layout();
  for (int i = 0; i < g_bench_n; ++i) {
    const RectStyle &s = RECT_PALETTE[i % RECT_PALETTE_SIZE];
    g->set_color(s.color, s.alpha);
    const PrimitivePlacement p = placement_for(layout, i);
    g->fill_rectangle({p.x0, p.y0}, {p.x1, p.y1});
  }
}

void draw_chars(ezgl::renderer *g)
{
  const GridLayout layout = current_layout();
  g->set_color(ezgl::BLACK);
  g->set_font_size(std::max(1, static_cast<int>(std::min(layout.cell_w, layout.cell_h))));
  for (int i = 0; i < g_bench_n; ++i) {
    const PrimitivePlacement p = placement_for(layout, i);
    char ch[2] = {static_cast<char>('A' + (i % 26)), '\0'};
    g->draw_text({p.cx, p.cy}, ch, layout.cell_w, layout.cell_h);
  }
}

// ---- test case table -------------------------------------------------------

struct TestCase {
  const char          *label;
  ezgl::draw_canvas_fn fn;
  int                  count;
  const char          *output_file;
};

static const TestCase TESTS[] = {
    // { "variadic rects   ", draw_rectangles_variadic,         1'000, "bench_rects_variadic.png"    },
    // { "variadic lines   ", draw_lines_variadic,         1'000, "bench_lines_variadic.png"    },
    { "solid   ",       draw_solid,               1'000, "draw_solid.png"    },
    // { "variadic   ",       draw_variadic,               1'000, "bench_variadic.png"    },
//  { "solid lines      ", draw_lines_solid,           200'000'000, "bench_lines_solid.png"       },
//  { "variadic lines   ", draw_lines_variadic,         1'000'000, "bench_lines_variadic.png"    },
  // { "variadic lines   ", draw_lines_variadic,         400'000'000, "bench_lines_variadic.png"    },
      //////////////////
    // { "solid lines      ", draw_lines_solid,              1000, "bench_lines_solid.png"       },
  // { "solid lines      ", draw_lines_solid,             10'000, "bench_lines_solid.png"       },
  // { "solid lines      ", draw_lines_solid,            100'000, "bench_lines_solid.png"       },
  // { "solid lines      ", draw_lines_solid,           1'000'000, "bench_lines_solid.png"       },
  // { "transparen lines ", draw_lines_transparent,        1000, "bench_lines_transparent.png" },
  // { "transparen lines ", draw_lines_transparent,       10'000, "bench_lines_transparent.png" },
  // { "transparen lines ", draw_lines_transparent,      100'000, "bench_lines_transparent.png" },
  // { "transparen lines ", draw_lines_transparent,     1'000'000, "bench_lines_transparent.png" },
  // { "solid rects      ", draw_rectangles_solid,         1000, "bench_rects_solid.png"       },
  // { "solid rects      ", draw_rectangles_solid,        10'000, "bench_rects_solid.png"       },
  // { "solid rects      ", draw_rectangles_solid,       100'000, "bench_rects_solid.png"       },
  // { "solid rects      ", draw_rectangles_solid,      1'000'000, "bench_rects_solid.png"       },
  // { "transparen rects ", draw_rectangles_transparent,   1000, "bench_rects_transparent.png" },
  // { "transparen rects ", draw_rectangles_transparent,  10'000, "bench_rects_transparent.png" },
  // { "transparen rects ", draw_rectangles_transparent, 100'000, "bench_rects_transparent.png" },
  // { "transparen rects ", draw_rectangles_transparent,1'000'000, "bench_rects_transparent.png" },
  // { "variadic lines   ", draw_lines_variadic,            1000, "bench_lines_variadic.png"    },
  // { "variadic lines   ", draw_lines_variadic,           10'000, "bench_lines_variadic.png"    },
  // { "variadic lines   ", draw_lines_variadic,          100'000, "bench_lines_variadic.png"    },
  // { "variadic lines   ", draw_lines_variadic,         1'000'000, "bench_lines_variadic.png"    },
  // { "variadic rects   ", draw_rectangles_variadic,       1000, "bench_rects_variadic.png"    },
  // { "variadic rects   ", draw_rectangles_variadic,      10'000, "bench_rects_variadic.png"    },
  // { "variadic rects   ", draw_rectangles_variadic,     100'000, "bench_rects_variadic.png"    },
  // { "variadic rects   ", draw_rectangles_variadic,    1'000'000, "bench_rects_variadic.png"    },
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

  static int g_headless_t = 0;
  auto headless_dispatch = [](ezgl::renderer *g) {
    TESTS[g_headless_t].fn(g);
  };

  app.add_canvas("headless_canvas", headless_dispatch, WORLD, ezgl::WHITE);
  ezgl::canvas *c = app.get_canvas("headless_canvas");

  for (int t = 0; t < N_TESTS; ++t) {
    const TestCase &tc = TESTS[t];
    g_headless_t = t;
    g_bench_n    = tc.count;

    // Warm-up: one throwaway render to prime caches.
    c->draw_offscreen(IMG_W, IMG_H);

    auto t0 = std::chrono::high_resolution_clock::now();
    c->draw_offscreen(IMG_W, IMG_H);
    auto t1 = std::chrono::high_resolution_clock::now();

    double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    std::cout << tc.label << "(" << g_bench_n << "): " << ms << " ms\n";

    std::string label(tc.label);
    label.erase(label.find_last_not_of(" \t") + 1);
    write_result("headless:" + std::to_string(g_bench_n) + " " + label, ms);

    // Save PNG once (untimed) for visual verification.
    c->print_png(tc.output_file, IMG_W, IMG_H);
  }
}

// ---- UI mode ---------------------------------------------------------------

static int                g_current_test  = 0;
static ezgl::application *g_app           = nullptr;
static double             g_last_frame_ms = -1.0;

static void draw_dispatch(ezgl::renderer *g)
{
  g_bench_n = TESTS[g_current_test].count;

  auto t0 = std::chrono::high_resolution_clock::now();
  TESTS[g_current_test].fn(g);
  auto t1 = std::chrono::high_resolution_clock::now();
  g_last_frame_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

  {
    std::string label(TESTS[g_current_test].label);
    label.erase(label.find_last_not_of(" \t") + 1);
    write_result("ui:" + std::to_string(g_bench_n) + " " + label, g_last_frame_ms);
  }

  if (g_app) {
    std::ostringstream oss;
    oss << TESTS[g_current_test].label
        << " | " << g_bench_n << " primitives"
        << " | " << std::fixed << std::setprecision(2) << g_last_frame_ms << " ms";
    g_app->update_message(oss.str());
  }
}

static void switch_test(ezgl::application *app, int delta)
{
  g_current_test = (g_current_test + delta + N_TESTS) % N_TESTS;
  app->change_canvas_world_coordinates("MainCanvas", WORLD);
  app->refresh_drawing();
}

static void ui_setup(ezgl::application *app, bool /*new_window*/)
{
  g_app = app;

  app->create_button("Prev", 6, [](GtkWidget *, ezgl::application *a) { switch_test(a, -1); });
  app->create_button("Next", 7, [](GtkWidget *, ezgl::application *a) { switch_test(a, +1); });

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
