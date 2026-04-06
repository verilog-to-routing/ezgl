# Deferred Renderer — Implementation Plan

## Overview of existing code

| file | role |
|---|---|
| `include/ezgl/graphics.hpp` | `ezgl::renderer` — public API + private style state |
| `src/graphics.cpp` | All renderer method bodies; draw calls go through `Painter` |
| `include/ezgl/qt/painter.hpp` | `ezgl::Painter` (wraps `QPainter`) — low-level draw API |
| `src/qt/painter.cpp` | `Painter` method bodies |
| `include/ezgl/canvas.hpp` | `ezgl::canvas` — owns `Painter*`, calls draw callback, owns `renderer` |
| `src/canvas.cpp` | `canvas::draw_surface` — creates renderer, calls `m_draw_callback(renderer)` |

The draw path today:
```
canvas::draw_surface()
  → new renderer(m_painter, transform, m_camera, m_surface)
  → m_draw_callback(renderer)          ← user code calls draw_line / fill_rectangle etc.
      → renderer::draw_line()
          → m_painter->move_to() / line_to() / stroke()   ← one QPainterPath per call
```

The deferred path will be:
```
canvas::draw_surface()
  → new deferred_renderer(m_painter, transform, m_camera, m_surface)
  → m_draw_callback(deferred_renderer)  ← same user code, no changes
      → deferred_renderer::draw_line()  ← appends QLineF to batch, no QPainter call
  → deferred_renderer::flush()          ← one QPainter::drawLines() per style group
```

---

## Step 1 — Define `LineStyle` and `RectStyle` key structs

**File:** `include/ezgl/qt/deferred_renderer.hpp` (new)

Pack the four style fields into a `uint64_t` for O(1) hash and comparison.

```cpp
struct LineStyle {
    uint32_t color_rgba;  // r | g<<8 | b<<16 | a<<24
    uint16_t line_width;
    uint8_t  line_cap;    // Qt::PenCapStyle cast to uint8
    uint8_t  line_dash;   // ezgl::line_dash cast to uint8

    uint64_t key() const {
        return uint64_t(color_rgba)
             | (uint64_t(line_width) << 32)
             | (uint64_t(line_cap)   << 48)
             | (uint64_t(line_dash)  << 56);
    }
};

// Filled rects only depend on color; line fields are zeroed.
struct FillStyle {
    uint32_t color_rgba;
    uint64_t key() const { return color_rgba; }
};
```

No separate hasher needed — use `std::unordered_map<uint64_t, ...>` directly.

---

## Step 2 — Define batch storage

**File:** `include/ezgl/qt/deferred_renderer.hpp`

```cpp
struct LineBatch {
    LineStyle   style;
    std::vector<QLineF> lines;
};

struct FillRectBatch {
    FillStyle   style;
    std::vector<QRectF> rects;
};

struct DrawRectBatch {
    LineStyle   style;
    std::vector<QRectF> rects;
};
```

Frame state:
```cpp
std::vector<LineBatch>     m_line_batches;
std::vector<FillRectBatch> m_fill_rect_batches;
std::vector<DrawRectBatch> m_draw_rect_batches;

// fast lookup: style key → index into the vector above
std::unordered_map<uint64_t, size_t> m_line_idx;
std::unordered_map<uint64_t, size_t> m_fill_rect_idx;
std::unordered_map<uint64_t, size_t> m_draw_rect_idx;
```

Insertion:
```cpp
void add_line(const LineStyle& s, QLineF line) {
    uint64_t k = s.key();
    auto it = m_line_idx.find(k);
    if (it == m_line_idx.end()) {
        m_line_idx[k] = m_line_batches.size();
        m_line_batches.push_back({s, {}});
        it = m_line_idx.find(k);
    }
    m_line_batches[it->second].lines.push_back(line);
}
```

Same pattern for fill_rect and draw_rect.

---

## Step 3 — Create `deferred_renderer` class

**File:** `include/ezgl/qt/deferred_renderer.hpp` + `src/qt/deferred_renderer.cpp` (new)

```cpp
class deferred_renderer : public renderer {
public:
    deferred_renderer(Painter* painter, transform_fn transform,
                      camera* cam, QImage* surface);

    // Override only the hot-path primitives:
    void draw_line(point2d start, point2d end);
    void fill_rectangle(point2d start, point2d end);
    void draw_rectangle(point2d start, point2d end);

    // Flush all collected batches to QPainter.
    void flush();

    // Clear all batches (called at start of each frame).
    void reset();

private:
    LineStyle    current_line_style() const;
    FillStyle    current_fill_style() const;

    // batch storage (Step 2)
    ...
};
```

`draw_line` override:
```cpp
void deferred_renderer::draw_line(point2d start, point2d end) {
    point2d s = m_transform(start);
    point2d e = m_transform(end);
    add_line(current_line_style(), QLineF(s.x, s.y, e.x, e.y));
}
```

The world→screen transform is applied here at collection time.
`renderer::draw_line` (the original) is NOT called.

Non-overridden primitives (arcs, polygons, text) fall through to
`renderer`'s existing implementation and are drawn immediately as before.

---

## Step 4 — Implement `flush()`

**File:** `src/qt/deferred_renderer.cpp`

```cpp
void deferred_renderer::flush() {
    // --- lines ---
    for (const auto& batch : m_line_batches) {
        QPen pen;
        pen.setColor(QColor(/* unpack batch.style.color_rgba */));
        pen.setWidth(batch.style.line_width);
        pen.setCapStyle(Qt::PenCapStyle(batch.style.line_cap));
        if (batch.style.line_dash != 0)
            /* set dash pattern */;
        m_painter->setPen(pen);
        m_painter->setBrush(Qt::NoBrush);
        m_painter->drawLines(batch.lines.data(), int(batch.lines.size()));
    }

    // --- filled rects ---
    for (const auto& batch : m_fill_rect_batches) {
        QColor c(/* unpack */);
        m_painter->setPen(Qt::NoPen);
        m_painter->setBrush(QBrush(c));
        m_painter->drawRects(batch.rects.data(), int(batch.rects.size()));
    }

    // --- outline rects ---
    for (const auto& batch : m_draw_rect_batches) {
        QPen pen(/* from style */);
        m_painter->setPen(pen);
        m_painter->setBrush(Qt::NoBrush);
        m_painter->drawRects(batch.rects.data(), int(batch.rects.size()));
    }

    reset();
}
```

One `setPen` per batch instead of one per primitive. One `drawLines` call
instead of N `stroke()` calls.

---

## Step 5 — Wire `deferred_renderer` into `canvas`

**File:** `src/canvas.cpp`

In `canvas::draw_surface()`, replace:
```cpp
renderer r(m_painter, transform, &m_camera, m_surface);
m_draw_callback(&r);
```
with:
```cpp
deferred_renderer r(m_painter, transform, &m_camera, m_surface);
m_draw_callback(&r);
r.flush();
```

`deferred_renderer` inherits from `renderer`, so the callback signature
`void(*)(renderer*)` is unchanged — no call sites need modification.

Also wire into `canvas::render_to_image()` (used by `print_png`) the same way.

---

## Step 6 — Add to CMakeLists

**File:** `CMakeLists.txt`

Add `src/qt/deferred_renderer.cpp` to the Qt source list (guarded by
`if(EZGL_QT)`).

---

## Step 7 — Validate with renderer-stress-bench

Run the existing bench (headless + UI) before and after and compare results
files to confirm improvement and no visual regression (PNG output must be
pixel-identical for solid primitives; alpha-blended results may differ by ±1
due to rounding order).

---

## File summary

| action | file |
|---|---|
| **new** | `include/ezgl/qt/deferred_renderer.hpp` |
| **new** | `src/qt/deferred_renderer.cpp` |
| **modify** | `src/canvas.cpp` — swap renderer for deferred_renderer in draw_surface + render_to_image |
| **modify** | `CMakeLists.txt` — add new .cpp |
| no change | `include/ezgl/graphics.hpp` |
| no change | `src/graphics.cpp` |
| no change | `include/ezgl/qt/painter.hpp` |
| no change | `src/qt/painter.cpp` |

---

## Risk notes

- **Painter's-algorithm ordering** is preserved within each type (lines on top
  of rects) but **not across types** — if user code interleaves
  `draw_line` / `fill_rectangle` calls expecting painter ordering between them,
  the result will differ. This is acceptable for VTR's draw callback patterns
  but should be documented.
- `fill_poly`, `draw_arc`, `fill_arc`, and `draw_text` are left as immediate-mode
  calls in this iteration — they are not hot path in VTR.
- The `render_to_image` (headless PNG) path must also be switched to
  `deferred_renderer`, otherwise the bench will show improvement only in UI mode.
