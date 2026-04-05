# Renderer Stress Benchmark — Requirements

## Goal
Measure the rendering performance of the ezgl renderer for individual primitive types.
Supports two modes: **headless** (save PNGs + print timing) and **UI** (interactive window with live frame timing in status bar).

## Modes

### Headless (default)
```
renderer-stress-bench
```
Runs all 5 test cases sequentially without opening any window. Saves one PNG per test and prints wall-clock time to stdout:
```
lines solid       (10000): X.XX ms  ->  bench_lines_solid.png
lines transparen  (10000): X.XX ms  ->  bench_lines_transparent.png
rects solid       (10000): X.XX ms  ->  bench_rects_solid.png
rects transparen  (10000): X.XX ms  ->  bench_rects_transparent.png
chars             (10000): X.XX ms  ->  bench_chars.png
```

### UI
```
renderer-stress-bench --ui [N]
```
Opens a window showing test case N (default 0). The status bar shows:
```
<test label> | 10000 primitives | X.XX ms
```
The timing is updated every frame (every redraw). **Prev** / **Next** sidebar buttons cycle through test cases.

## Requirements

### R1 — Primitive test cases
Five isolated test cases, each drawing **10 000** primitives of one type:

| N | Label                | Output file                    | Primitive         | Alpha        |
|---|----------------------|--------------------------------|-------------------|--------------|
| 0 | lines solid          | `bench_lines_solid.png`        | lines             | 255 (opaque) |
| 1 | lines transparent    | `bench_lines_transparent.png`  | lines             | 128 (50%)    |
| 2 | rects solid          | `bench_rects_solid.png`        | filled rectangles | 255 (opaque) |
| 3 | rects transparent    | `bench_rects_transparent.png`  | filled rectangles | 128 (50%)    |
| 4 | chars                | `bench_chars.png`              | single characters | 255 (opaque) |

### R2 — Non-overlapping layout
Primitives are placed in a 100 × 100 grid of 10 × 10 world-unit cells.
Output image size: **1000 × 1000** pixels. Every cell contains exactly one primitive.

### R3 — Isolated canvases (headless)
Each test case uses its own `ezgl::canvas` (unique canvas id). No test shares state.

### R4 — Frame timing in UI mode
The draw callback measures its own wall-clock time and updates the status bar after every frame via `ezgl::application::update_message`.

### R5 — No API changes
Public API of `ezgl::canvas`, `ezgl::renderer`, and GTK-compat wrappers is not modified.
Uses `ezgl::application::add_canvas` + `canvas::print_png` (headless) and `application::run` (UI).

### R6 — Dual-build compatibility
Compiles and runs correctly under both the GTK build and the Qt (`EZGL_QT`) build.
GTK build embeds `main.ui` via GLib resources (`.gresource.xml` + `resources.C`).
Qt build embeds `main.ui` via Qt resources (`resource.qrc`).

### R7 — Argument handling
| Argument    | Behaviour                                      |
|-------------|------------------------------------------------|
| *(none)*    | Headless mode                                  |
| `--ui`      | UI mode, test case 0                           |
| `--ui N`    | UI mode, test case N                           |
| `--help`    | Print usage and exit 0                         |
| *(unknown)* | Print error + usage and exit 1                 |
