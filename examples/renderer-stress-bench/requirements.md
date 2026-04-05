# Renderer Stress Benchmark — Requirements

## Goal
Measure the rendering performance of the ezgl renderer for individual primitive types using the existing `canvas`/`print_png` infrastructure, without any window or UI.

## Requirements

### R1 — Primitive test cases
Five isolated test cases, each drawing **10 000** primitives of one type:

| Label                | Output file                    | Primitive         | Alpha |
|----------------------|--------------------------------|-------------------|-------|
| lines solid          | `bench_lines_solid.png`        | lines             | 255 (opaque) |
| lines transparent    | `bench_lines_transparent.png`  | lines             | 128 (50%)    |
| rects solid          | `bench_rects_solid.png`        | filled rectangles | 255 (opaque) |
| rects transparent    | `bench_rects_transparent.png`  | filled rectangles | 128 (50%)    |
| chars                | `bench_chars.png`              | single characters | 255 (opaque) |

### R2 — Non-overlapping layout
Primitives within each test are placed in a 100 × 100 grid of 10 × 10 world-unit cells.
The output image size is **1000 × 1000** pixels (≤ maximum allowed).
Every cell contains exactly one primitive; no two primitives overlap.

### R3 — Isolated images
Each test case uses its own `ezgl::canvas` (unique canvas id) and saves to a separate PNG file.
No test shares state with another.

### R4 — Timing
Wall-clock time for each test case (the `print_png` call) is measured and printed to stdout:
```
lines solid       (10000): X.XX ms  ->  bench_lines_solid.png
lines transparent (10000): X.XX ms  ->  bench_lines_transparent.png
rects solid       (10000): X.XX ms  ->  bench_rects_solid.png
rects transparent (10000): X.XX ms  ->  bench_rects_transparent.png
chars             (10000): X.XX ms  ->  bench_chars.png
```

### R5 — No API changes
The public API of `ezgl::canvas`, `ezgl::renderer`, and all GTK-compatibility wrappers must not be modified. The benchmark builds on top of the existing codebase via `ezgl::application::add_canvas` + `canvas::print_png`.

### R6 — Dual-build compatibility
The file compiles and runs correctly under both the GTK build and the Qt (`EZGL_QT`) build.
Under the Qt build a `QApplication` instance is required before any rendering.

## Output files
| File                          | Contents                              |
|-------------------------------|---------------------------------------|
| `bench_lines_solid.png`       | 10 000 opaque blue lines           |
| `bench_lines_transparent.png` | 10 000 50%-transparent blue lines  |
| `bench_rects_solid.png`       | 10 000 opaque red rectangles       |
| `bench_rects_transparent.png` | 10 000 50%-transparent red rects   |
| `bench_chars.png`             | 10 000 black characters            |
