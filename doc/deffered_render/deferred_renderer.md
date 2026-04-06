# Deferred Renderer — Feature Requirements

## Motivation

The benchmark data shows that QPainter's per-call overhead dominates at high
primitive counts. The key insight is that the existing `renderer` API is
immediate-mode: every `draw_line` / `fill_rectangle` call issues one QPainter
draw call. QPainter supports batch APIs (`drawLines(QLineF*, int)`,
`drawRects(QRectF*, int)`) that amortise the per-call cost significantly.

A deferred renderer collects primitives during a draw callback and flushes them
in batches, grouped by type and style, at the end of the frame.

---

## Requirements

### 1. Primitive grouping

All primitives emitted during a single draw callback must be grouped by:

- **primitive type** — `line`, `fill_rect`, `draw_rect`, `fill_arc`, `draw_arc`,
  `fill_poly`, `text`
- **draw style** — the complete set of active style attributes at the time the
  primitive was submitted (see §3)

Primitives that share the same type **and** the same style key are collected into
a contiguous batch. Primitives with different styles are stored in separate
batches. Insertion order across batches must be preserved so that the rendered
output matches the immediate-mode result (later primitives paint over earlier ones
of the same style only if they were submitted later).

### 2. QPainter batch flush

At frame end (`flush()`), the deferred renderer iterates over all collected
batches in submission order and issues one QPainter batch call per batch:

| primitive type    | QPainter batch API                          |
|-------------------|---------------------------------------------|
| `line`            | `QPainter::drawLines(const QLineF*, int)`   |
| `fill_rect`       | `QPainter::fillRect` per item or `drawRects` with brush set |
| `draw_rect`       | `QPainter::drawRects(const QRectF*, int)`   |
| `fill_poly`       | `QPainter::drawPolygon` per item (no batch API; still grouped for pen/brush setup amortisation) |
| `fill_arc` / `draw_arc` | `QPainter::drawArc` per item (same as poly) |
| `text`            | `QPainter::drawText` per item               |

Before each batch flush, the renderer sets the pen/brush once for the whole
batch. This eliminates redundant QPainter state changes between primitives.

### 3. Style key

A style key uniquely identifies the complete drawing state that affects visual
output. Two primitives share a batch if and only if their style keys are equal.

#### 3.1 Style fields

From `ezgl::renderer` private state:

| field                | type           | size    |
|----------------------|----------------|---------|
| `current_color`      | `ezgl::color`  | 4 bytes (r, g, b, a as `uint8_t`) |
| `current_line_width` | `int`          | 4 bytes |
| `current_line_cap`   | `line_cap`     | 4 bytes (enum backed by `Qt::PenCapStyle`) |
| `current_line_dash`  | `line_dash`    | 4 bytes (enum: `none` / `asymmetric_5_3`) |

Text and arc primitives additionally carry:

| field                    | type           |
|--------------------------|----------------|
| `rotation_angle`         | `double`       |
| `horiz_justification`    | `justification`|
| `vert_justification`     | `justification`|
| font family              | `std::string`  |
| `font_slant`             | enum           |
| `font_weight`            | enum           |
| font size                | `double`       |

#### 3.2 Fast hash for lines and rectangles

Lines and filled rectangles are the hot path. Their style is fully described by
the four integer fields above (16 bytes total). Pack them into a single `uint64_t`
for O(1) comparison and hashing:

```
┌────────┬────────────┬──────────┬───────────┐
│ color  │ line_width │ line_cap │ line_dash │
│ 32 bit │   16 bit   │  8 bit   │   8 bit   │
└────────┴────────────┴──────────┴───────────┘
```

```cpp
struct LineStyle {
    uint32_t color_rgba;   // r | g<<8 | b<<16 | a<<24
    uint16_t line_width;   // clamped to uint16 (widths > 65535 are not realistic)
    uint8_t  line_cap;     // cast from line_cap enum
    uint8_t  line_dash;    // cast from line_dash enum

    uint64_t key() const {
        return (uint64_t)color_rgba
             | ((uint64_t)line_width << 32)
             | ((uint64_t)line_cap   << 48)
             | ((uint64_t)line_dash  << 56);
    }

    bool operator==(const LineStyle& o) const { return key() == o.key(); }
};
```

Use `uint64_t` directly as the hash map key — integer hash maps (`std::unordered_map<uint64_t, ...>`) are faster than struct-key maps with custom hashers because the default `std::hash<uint64_t>` is a single multiply on most STL implementations.

For filled rectangles the same `LineStyle` key applies (only color matters; line
attributes are irrelevant for fills — `line_width`, `line_cap`, `line_dash` are
set to zero in the key).

#### 3.3 Text and arc style key

Text and arcs carry a larger style. Use a flat POD struct with `memcmp` equality
and `std::hash` over its bytes, or concatenate all fields into a `std::string`
key only when a text/arc primitive is first encountered (they are not hot path).

### 4. Batch storage

```
frame batches = ordered list of (StyleKey, PrimitiveType, geometry_vector)
```

Internally maintain:

```cpp
// insertion-order list of batch handles (for flush ordering)
std::vector<BatchHandle> batch_order;

// fast lookup: (type, key) → index into batch_order
std::unordered_map<uint64_t, size_t> batch_index;  // for line/rect
```

When a primitive arrives:
1. Compute `key = style.key() ^ (uint64_t(primitive_type) << 60)` to
   distinguish lines from rects sharing the same style.
2. Look up `key` in `batch_index`.
3. If found, append geometry to the existing batch's vector.
4. If not found, push a new batch onto `batch_order` and insert into
   `batch_index`.

This gives O(1) amortised insertion.

### 5. Coordinate system

Primitives are stored in **screen coordinates** (post-transform). The world→screen
transform is applied at submission time, not at flush time. This keeps flush code
simple and avoids re-applying the camera transform during batch rendering.

### 6. API compatibility

The deferred renderer must implement the same public interface as `ezgl::renderer`
so existing draw callbacks require no changes. It is a drop-in replacement
activated per-canvas via a compile-time or runtime flag.

```cpp
// Drop-in: same base class or same duck-typed interface
class deferred_renderer : public renderer { ... };
```

`flush()` is called by the canvas after the draw callback returns, before
`QPainter::end()`.

### 7. Out of scope

- Text batching beyond pen/font-setup amortisation (QPainter has no bulk text API)
- Cross-frame caching / dirty-region tracking
- Cairo/GTK backend (deferred renderer targets Qt only)
- Clipping-region awareness during collection (clipping is still applied by
  QPainter at flush time)
