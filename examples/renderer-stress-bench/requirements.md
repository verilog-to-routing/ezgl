# Renderer Stress Benchmark — Requirements

## Goal
Measure the rendering performance of the ezgl renderer for individual primitive types.
Supports two modes: **headless** (save PNGs + print timing) and **UI** (interactive window with live frame timing in status bar).

## Modes

### Headless (default)
```
renderer-stress-bench
```
Runs all test cases sequentially without opening any window.
For each test case the draw callback is invoked **twice** — first as a warm-up (untimed), then once for measurement.
The PNG is saved separately after timing so file I/O is not included in the reported time.
Prints wall-clock draw time to stdout:
```
solid lines       (1000000):  X.XX ms
transparen lines  (1000000):  X.XX ms
solid rects       (1000000):  X.XX ms
transparen rects  (1000000):  X.XX ms
variadic lines    (1000000):  X.XX ms
variadic rects    (1000000):  X.XX ms
```

### UI
```
renderer-stress-bench --ui [N]
```
Opens a window showing test case N (default 0). The status bar shows:
```
<test label> | 1000000 primitives | X.XX ms
```
The timing is updated every frame (every redraw). **Prev** / **Next** sidebar buttons cycle through test cases.

---

## Requirements

### R1 — Fixed-style test cases
Existing test cases, each drawing **N** primitives of one type with a single constant style.
N is swept over {1 000, 10 000, 100 000, 1 000 000}.

| Label                | Primitive         | Color      | Alpha        |
|----------------------|-------------------|------------|--------------|
| solid lines          | lines             | BLUE       | 255 (opaque) |
| transparen lines     | lines             | BLUE       | 128 (50%)    |
| solid rects          | filled rectangles | RED        | 255 (opaque) |
| transparen rects     | filled rectangles | RED        | 128 (50%)    |

### R2 — Variadic-style test cases
New test cases that change drawing style on every primitive.
Purpose: exercise the renderer's style-switch path and provide a meaningful
workload for benchmarking the deferred renderer's batching benefit.
N is swept over the same values as R1.

| Label                | Primitive         | Style variation                              |
|----------------------|-------------------|----------------------------------------------|
| variadic lines       | lines             | color + line width cycle (see §R2.1)         |
| variadic rects       | filled rectangles | color cycle (see §R2.1)                      |

#### R2.1 — Deterministic style palette
Style for primitive `i` is chosen as `STYLES[i % K]` where `STYLES` is a
fixed compile-time array and `K` is its length.  
The palette must never use `rand()` or any non-deterministic source — the same
N always produces the same pixel output, making runs directly comparable.

**Line style palette** (K = 8, cycles color × line-width):

| idx | color           | alpha | line width |
|-----|-----------------|-------|------------|
| 0   | BLUE            | 255   | 1          |
| 1   | RED             | 255   | 1          |
| 2   | GREEN           | 255   | 1          |
| 3   | ORANGE          | 255   | 2          |
| 4   | BLUE            | 128   | 1          |
| 5   | RED             | 128   | 1          |
| 6   | GREEN           | 128   | 1          |
| 7   | ORANGE          | 128   | 2          |

**Rect fill palette** (K = 8, cycles color × alpha):

| idx | color           | alpha |
|-----|-----------------|-------|
| 0   | BLUE            | 255   |
| 1   | RED             | 255   |
| 2   | GREEN           | 255   |
| 3   | CYAN            | 255   |
| 4   | BLUE            | 128   |
| 5   | RED             | 128   |
| 6   | GREEN           | 128   |
| 7   | CYAN            | 128   |

With K = 8 and N = 1 000 000, the deferred renderer will produce exactly
8 batches per frame — one per distinct style. This is the intended load to
measure batching efficiency.

#### R2.2 — Style-switch frequency variants (future)
To quantify batching benefit across different switch rates, additional palette
sizes K ∈ {2, 4, 8, 64, 1000} may be added later, each as its own test case.
Not required for the initial implementation.

### R3 — Non-overlapping layout
Primitives are placed in a 1000 × 1000 grid of 1 × 1 world-unit cells.
Output image size: **1000 × 1000** pixels. Every cell contains exactly one primitive.

### R4 — Separated draw and encode timing
Headless mode times only the draw callback (and deferred flush if applicable).
`canvas::draw_offscreen()` is called (timed) for measurement only.
`canvas::print_png()` is called separately (untimed) for file output only.
Note: `print_png()` re-runs the draw callback independently; there is no shared
image cache between `draw_offscreen()` and the `print_*` functions.

### R5 — Frame timing in UI mode
The draw callback measures its own wall-clock time and updates the status bar
after every frame via `ezgl::application::update_message`.

### R6 — Result file format
Each headless run appends/overwrites entries in `renderer-stress-bench-results.txt`:
```
headless:1000000 solid lines      957.25 ms
headless:1000000 variadic lines   1243.80 ms
ui:1000000 solid lines            880.96 ms
```
Keys are padded to a uniform column width so the ms values align.
New entries with the same key overwrite old ones; other entries are preserved.

### R7 — Dual-build compatibility
Compiles and runs correctly under both the GTK build and the Qt (`EZGL_QT`) build.
GTK build embeds `main.ui` via GLib resources (`.gresource.xml` + `resources.C`).
Qt build embeds `main.ui` via Qt resources (`resource.qrc`).

### R8 — Argument handling
| Argument    | Behaviour                                      |
|-------------|------------------------------------------------|
| *(none)*    | Headless mode                                  |
| `--ui`      | UI mode, test case 0                           |
| `--ui N`    | UI mode, test case N                           |
| `--help`    | Print usage and exit 0                         |
| *(unknown)* | Print error + usage and exit 1                 |
