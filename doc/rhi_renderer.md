# Qt RHI Renderer — Implementation Plan

## 1. Problem Statement

The current `deferred_renderer` batches QPainter calls and achieves a 10–100× speedup
over immediate mode. However, QPainter is a **CPU software rasterizer** — every pixel is
computed on the CPU. At 100 million line segments, even batched draw calls are bound by:

- CPU rasterization bandwidth (~1–5 M lines/s on modern hardware)
- QImage pixel write-back to the GPU for display (full frame upload every repaint)
- No hardware line anti-aliasing

A GPU-backed renderer using **Qt RHI** removes all three bottlenecks:
the GPU rasterizes lines in parallel, there is no CPU→GPU upload of the final pixels,
and MSAA is free on modern hardware.

**Target:** render 100 million line segments at interactive frame rates (>30 fps)
on pan/zoom on Linux, Windows, and macOS without changing the public `ezgl::renderer` API.

---

## 2. Qt RHI Overview

`QRhi` (Qt 6.6+, public API) is Qt's portable GPU abstraction. A single C++ code
path produces correct GPU commands on all backends:

| Platform | Primary backend | Fallback |
|---|---|---|
| Linux | Vulkan 1.1 | OpenGL 3.2 |
| Windows | Direct3D 12 | Vulkan / OpenGL |
| macOS / iOS | Metal | — |

`QRhiWidget` (Qt 6.7+) wraps `QRhi` in a `QWidget` subclass: it handles the
swapchain, resize, and repaint loop, and exposes two virtual methods:

```cpp
void initialize(QRhiCommandBuffer *cb);   // called once; create pipelines/buffers
void render(QRhiCommandBuffer *cb);       // called each repaint; upload + draw
```

This is the integration point we target.

---

## 3. Why Not Keep Growing the QPainter Path

| Constraint | QPainter | Qt RHI |
|---|---|---|
| Rasterization | CPU (single core) | GPU (thousands of cores) |
| Line throughput | ~5 M lines/s | ~500 M lines/s |
| Final blit | Full QImage copy to screen | Zero-copy (GPU renders directly to display) |
| Anti-aliasing | Software (slow) | MSAA / FXAA (free) |
| Cross-platform | Any Qt | Vulkan/Metal/D3D12 (Qt 6.6+) |
| Fallback if GPU unavailable | Already works | Degrade to deferred_renderer |

---

## 4. Architecture Overview

```
ezgl::canvas
    ├── deferred_renderer   (QPainter path, fallback)
    └── rhi_renderer        (GPU path, new)
            │
            └── RhiCanvasWidget : QRhiWidget
                    ├── QRhiBuffer*  m_vbuf   (dynamic; vertex staging)
                    ├── QRhiBuffer*  m_ubuf   (per-frame transform UBO)
                    ├── QRhiGraphicsPipeline* m_line_pso
                    ├── QRhiGraphicsPipeline* m_rect_fill_pso
                    └── QRhiGraphicsPipeline* m_rect_outline_pso
```

### 4.1 Rendering Layers

Three GPU pipelines, each gets its own vertex stream:

1. **Line pipeline** — topology `Lines`; vertex = `(x, y, r, g, b, a)`
2. **Filled rect pipeline** — topology `Triangles` (2 triangles per rect);
   vertex = `(x, y, r, g, b, a)`
3. **QPainter overlay** — arcs, polygons, text remain on QPainter;
   composited on top of the RHI frame via `QRhiWidget::grabFramebuffer()`
   followed by `QPainter::drawImage`

### 4.2 Coordinate System

The world-to-NDC transform is uploaded as a `mat4` UBO updated every frame.
Lines stay in **world coordinates** inside the vertex buffer; the shader applies
the transform. This means zoom/pan only needs a UBO update — no vertex re-upload.

```
World coords (float32)  →  vertex buffer (stays static between redraws)
                        ×  UBO mat4 (updated on zoom/pan)
                        =  NDC clip coords
```

### 4.3 Spatial Culling (CPU-side)

100 M lines × 8 bytes (2×float32 per endpoint) = 1.6 GB vertex data.
Uploading the full dataset each frame is not feasible. Solution:

- Build a **uniform spatial grid** once when data is loaded.
- Each repaint: query grid for lines intersecting the current viewport.
- Upload only the visible subset to the GPU dynamic vertex buffer.
- At 1920×1080, visible non-overlapping lines ≤ ~2 M (pixel budget).

This reduces per-frame GPU upload from ~2 GB to ~40 MB (2 M lines × 20 bytes).

---

## 5. GPU Vertex Layout

### 5.1 Line vertex (20 bytes per line, 10 bytes per vertex)

```cpp
struct LineVertex {
    float    x, y;           // world-space endpoint (8 bytes)
    uint8_t  r, g, b, a;    // RGBA color (4 bytes, packed)
};                           // total: 12 bytes per vertex, 24 bytes per line

// Two vertices per line segment (no index buffer needed).
```

All lines with different colors share the same vertex buffer. One draw call
submits all visible lines regardless of color — the color is per-vertex in the
vertex attribute, not a uniform.

### 5.2 Filled rect vertex (same layout, 6 vertices per rect = 2 triangles)

```
v0───v1
│  ╲  │
v2  v3─v4
        │
       v5
```

Alternatively, 4 vertices + index buffer (saves 33% vertex memory):

```cpp
// indices: 0,1,2, 2,1,3  — one quad = two triangles
```

### 5.3 Uniform buffer (per-frame, shared by all pipelines)

```glsl
layout(std140, binding = 0) uniform Frame {
    mat4  world_to_clip;     // updated on pan/zoom
    float pixel_width;       // viewport pixel width (for LOD)
    float pixel_height;
};
```

---

## 6. Shader Design

Shaders are written in GLSL 4.5 / GLSL ES 3.1 and compiled to SPIR-V via
`qsb` (Qt Shader Baker). Qt RHI cross-compiles SPIR-V to MSL (Metal) and
HLSL (D3D12) automatically at runtime.

### 6.1 Vertex Shader — lines and rects (`line.vert`)

```glsl
#version 450

layout(location = 0) in vec2 inPos;
layout(location = 1) in vec4 inColor;   // normalized float from uint8 attributes

layout(std140, binding = 0) uniform Frame {
    mat4  world_to_clip;
    float pixel_width;
    float pixel_height;
};

layout(location = 0) out vec4 v_color;

void main() {
    gl_Position = world_to_clip * vec4(inPos, 0.0, 1.0);
    v_color = inColor;
}
```

### 6.2 Fragment Shader (`line.frag`)

```glsl
#version 450

layout(location = 0) in  vec4 v_color;
layout(location = 0) out vec4 out_color;

void main() {
    out_color = v_color;
}
```

> For thick lines (width > 1 px), Phase 3 adds a geometry-expansion vertex
> shader that converts each line instance into a quad (4 vertices) with
> smooth AA edges.

### 6.3 Build step (CMake)

```cmake
find_package(Qt6 COMPONENTS ShaderTools REQUIRED)

qt6_add_shaders(ezgl "ezgl_shaders"
    BATCHABLE
    PRECOMPILE
    OPTIMIZED
    PREFIX "/ezgl/shaders"
    FILES
        shaders/line.vert
        shaders/line.frag
        shaders/rect_fill.vert
        shaders/rect_fill.frag
)
```

Compiled `.qsb` files are embedded in the Qt resource system and loaded at runtime:

```cpp
QShader vs = loadShader(":/ezgl/shaders/line.vert.qsb");
```

---

## 7. New Classes

### 7.1 `RhiCanvasWidget` (`include/ezgl/qt/rhi_canvas_widget.hpp` + `.cpp`)

Replaces `DrawingAreaWidget` when RHI is enabled.

```cpp
class RhiCanvasWidget : public QRhiWidget {
    Q_OBJECT
public:
    explicit RhiCanvasWidget(QWidget* parent = nullptr);

    // Called by rhi_renderer::flush() before update():
    void set_frame_data(std::vector<LineVertex>    lines,
                        std::vector<LineVertex>    fill_rect_verts,
                        std::vector<LineVertex>    draw_rect_verts,
                        QMatrix4x4               world_to_clip);

    // Called by canvas for resize tracking:
    void setResizeCallback(std::function<void(int,int)> cb);

protected:
    void initialize(QRhiCommandBuffer *cb) override;
    void render(QRhiCommandBuffer *cb) override;

private:
    // GPU resources (created in initialize()):
    std::unique_ptr<QRhiBuffer>               m_ubuf;
    std::unique_ptr<QRhiBuffer>               m_line_vbuf;
    std::unique_ptr<QRhiBuffer>               m_fill_vbuf;
    std::unique_ptr<QRhiBuffer>               m_draw_vbuf;
    std::unique_ptr<QRhiShaderResourceBindings> m_srb;
    std::unique_ptr<QRhiGraphicsPipeline>     m_line_pso;
    std::unique_ptr<QRhiGraphicsPipeline>     m_rect_fill_pso;
    std::unique_ptr<QRhiGraphicsPipeline>     m_rect_draw_pso;
    std::unique_ptr<QRhiRenderPassDescriptor> m_rp;

    // Frame data (set by rhi_renderer, consumed by render()):
    std::mutex                  m_frame_mutex;
    std::vector<LineVertex>     m_pending_lines;
    std::vector<LineVertex>     m_pending_fill_verts;
    std::vector<LineVertex>     m_pending_draw_verts;
    QMatrix4x4                  m_pending_mvp;
    bool                        m_frame_dirty = false;

    std::function<void(int,int)> m_resize_cb;

    static QShader loadShader(const char* path);
    void ensureBufferCapacity(std::unique_ptr<QRhiBuffer>& buf,
                              int required_bytes,
                              QRhiBuffer::Type type);
};
```

### 7.2 `rhi_renderer` (`include/ezgl/qt/rhi_renderer.hpp` + `.cpp`)

Inherits from `renderer`. Overrides the same hot-path methods as `deferred_renderer`,
but builds `LineVertex` vectors instead of `QLineF` vectors.

```cpp
class rhi_renderer : public renderer {
public:
    rhi_renderer(RhiCanvasWidget* widget,
                 transform_fn     transform,
                 camera*          cam);

    // Hot-path overrides:
    void draw_line(point2d start, point2d end) override;
    void fill_rectangle(point2d start, point2d end) override;
    void draw_rectangle(point2d start, point2d end) override;

    // Non-overridden fall-through: fill_poly, draw_arc, fill_arc, draw_text
    // These use the QPainter overlay path (see Phase 2).

    // Transfer collected geometry to RhiCanvasWidget and trigger repaint:
    void flush();

private:
    LineVertex make_vertex(point2d p) const;

    RhiCanvasWidget*         m_rhi_widget;
    std::vector<LineVertex>  m_lines;         // 2 vertices per line
    std::vector<LineVertex>  m_fill_verts;    // 6 vertices per rect
    std::vector<LineVertex>  m_draw_verts;    // 8 vertices per rect (outline = 4 lines)
};
```

### 7.3 `SpatialGrid` (`include/ezgl/spatial_grid.hpp` + `.cpp`)

CPU-side spatial index. Built once when data is loaded; queried each frame.

```cpp
class SpatialGrid {
public:
    // Build index from N line segments.
    // cell_size: grid cell size in world units.
    void build(const point2d* starts, const point2d* ends,
               size_t count, double cell_size);

    // Return indices of all line segments whose bounding box
    // intersects [viewport].
    std::vector<size_t> query(rectangle viewport) const;

private:
    struct Cell { std::vector<size_t> indices; };
    std::vector<Cell> m_cells;
    double m_cell_w, m_cell_h;
    int m_cols, m_rows;
    rectangle m_bounds;
    // line data stored externally by caller
};
```

---

## 8. Integration with `canvas`

### 8.1 `canvas::initialize()` — widget selection

```cpp
void canvas::initialize(QWidget* drawing_area) {
#ifdef EZGL_RHI
    if (auto* rhi_w = qobject_cast<RhiCanvasWidget*>(drawing_area)) {
        m_rhi_widget = rhi_w;
        rhi_w->setResizeCallback([this](int w, int h) {
            m_camera.update_widget(w, h);
            redraw();
        });
        return;  // no QPainter surface needed
    }
#endif
    // ... existing QPainter path unchanged
}
```

### 8.2 `canvas::redraw()` — renderer selection

```cpp
void canvas::redraw() {
#ifdef EZGL_RHI
    if (m_rhi_widget) {
        using namespace std::placeholders;
        rhi_renderer g(m_rhi_widget,
                       std::bind(&camera::world_to_screen, &m_camera, _1),
                       &m_camera);
        m_draw_callback(&g);
        g.flush();   // transfers data to RhiCanvasWidget, calls update()
        return;
    }
#endif
    // ... existing deferred_renderer path unchanged
}
```

The `EZGL_RHI` compile-time flag controls which path is compiled.
If `QRhiWidget` is unavailable (Qt < 6.7) or GPU init fails at runtime,
the code falls back silently to the `deferred_renderer` path.

---

## 9. Implementation Phases

### Phase 0 — Spatial Index (prerequisite, ~3 days)

**Goal:** CPU-side viewport culling to reduce 100 M → ≤ 2 M visible lines per frame.

Files:
- `include/ezgl/spatial_grid.hpp` (new)
- `src/spatial_grid.cpp` (new)

Steps:
1. Implement `SpatialGrid::build()` with a flat cell array.
   - Cell size = `world_size / sqrt(line_count)` heuristic (aim for ~50 lines per cell).
   - Each line is inserted into every cell its AABB intersects.
   - Build time: O(N) for axis-aligned geometry; O(N log N) worst case.
2. Implement `SpatialGrid::query(viewport)` — iterate over intersecting cells,
   deduplicate indices (use a per-frame generation counter, not a hash set).
3. Wire into `rhi_renderer::draw_line()`: call `query()` once before the user
   callback and only emit draw calls for visible lines.

**Alternative for callers that already know their visible set:** the spatial grid
is optional — the existing `rectangle_off_screen()` clip in `draw_line()` still works
as a per-call filter. The grid is only beneficial when the scene is entirely
pre-built (VTR's draw_routed_nets pattern).

---

### Phase 1 — Basic RHI Line Renderer (~5 days)

**Goal:** render thin (1 px) colored lines via GPU. Text/arcs/polygons still
use the QPainter fallback.

Files to create:
| File | Role |
|---|---|
| `include/ezgl/qt/rhi_canvas_widget.hpp` | `RhiCanvasWidget` class declaration |
| `src/qt/rhi_canvas_widget.cpp` | Resource creation, `initialize()`, `render()` |
| `include/ezgl/qt/rhi_renderer.hpp` | `rhi_renderer` class declaration |
| `src/qt/rhi_renderer.cpp` | Vertex collection, `flush()` |
| `shaders/line.vert` | Vertex shader (world coords → clip) |
| `shaders/line.frag` | Fragment shader (flat color) |

Files to modify:
| File | Change |
|---|---|
| `src/canvas.cpp` | Add `#ifdef EZGL_RHI` branch in `initialize()` and `redraw()` |
| `CMakeLists.txt` | Add `qt6_add_shaders`, new source files, `Qt6::RhiWidgets` |

Steps:
1. Create `shaders/line.vert` and `shaders/line.frag` (see §6).
2. Implement `RhiCanvasWidget::initialize()`:
   - Create `m_ubuf` (UniformBuffer, 64 + 8 bytes for mat4 + vec2).
   - Create `m_line_vbuf` as a `Dynamic` vertex buffer, initial size 2 M vertices.
   - Create `m_srb` with `m_ubuf` at binding 0.
   - Create `m_line_pso`:
     - Topology: `Lines`
     - Vertex layout: `vec2` position at stride 12, `unorm8x4` color.
     - Shaders: `line.vert.qsb`, `line.frag.qsb`.
     - Blend: `PorterDuff SrcOver` for alpha.
     - Depth: disabled (2D).
3. Implement `RhiCanvasWidget::render()`:
   - `QRhiResourceUpdateBatch* batch = rhi()->nextResourceUpdateBatch()`.
   - Upload `m_pending_mvp` to `m_ubuf`.
   - Upload `m_pending_lines` to `m_line_vbuf` (resize if needed).
   - `cb->beginPass(renderTarget(), Qt::transparent, {1,0}, batch)`.
   - `cb->setGraphicsPipeline(m_line_pso.get())`.
   - `cb->setShaderResources()`.
   - `cb->setVertexInput(0, 1, &{m_line_vbuf.get(), 0})`.
   - `cb->draw(pending_line_vertex_count)`.
   - `cb->endPass()`.
4. Implement `rhi_renderer`:
   - `draw_line()`: clip, transform to world (NOT screen), append 2 `LineVertex`.
   - `flush()`: compute `world_to_clip` matrix from `m_camera`, call
     `m_rhi_widget->set_frame_data(...)`, call `m_rhi_widget->update()`.
5. Wire into `canvas::redraw()` with `EZGL_RHI` guard.
6. Test: basic-application example should render all lines.

---

### Phase 2 — Complete Primitive Support (~3 days)

**Goal:** filled rects, outline rects, and QPainter overlay for text/arcs.

Steps:
1. Add `m_rect_fill_pso` (topology: `Triangles`) and `m_rect_draw_pso`
   (topology: `Lines`, 4 lines per rect = 8 vertices) to `RhiCanvasWidget`.
2. In `rhi_renderer`:
   - `fill_rectangle()`: emit 6 vertices (2 triangles) per rect to `m_fill_verts`.
   - `draw_rectangle()`: emit 8 vertices (4 lines) per rect to `m_draw_verts`.
3. **QPainter overlay for non-GPU primitives** (`fill_poly`, `draw_arc`, text):
   - Add a `deferred_renderer` member to `rhi_renderer` for fallback calls.
   - After RHI render, grab the RHI frame as `QImage` via
     `RhiCanvasWidget::grabFramebuffer()`.
   - Run the fallback `deferred_renderer` on a `QImage` sized to the widget.
   - Compose: blit the QPainter QImage on top of the RHI QImage.
   - This ensures correct painter's-algorithm ordering between GPU and CPU primitives.

   > For VTR's workload (lines dominate; text is sparse), this two-pass approach
   > adds ~0 ms overhead because the CPU primitives are few.

4. Update `RhiCanvasWidget::render()` to submit all three pipelines in order.

---

### Phase 3 — Performance Optimization (~5 days)

**Goal:** approach theoretical GPU throughput; eliminate per-frame re-upload for static data.

#### 3a. GPU-resident static geometry

For scenes where the line data does not change frame-to-frame (only the camera moves):

- Upload all visible-world lines to a `Static` `QRhiBuffer` once at load time.
- Store per-line color as a vertex attribute (already in our layout).
- Each frame: only update the `m_ubuf` transform matrix (64 bytes) — no vertex upload.
- Visibility culling stays CPU-side; the static buffer holds all lines.

If the spatial grid indicates < 2 M visible lines, upload those into a
`Dynamic` buffer as before. If camera is static (no pan/zoom since last frame),
skip all uploads.

```cpp
bool camera_changed = (m_last_mvp != current_mvp);
if (!camera_changed && !m_scene_dirty)
    return;  // nothing to upload; just re-submit the same draw
```

#### 3b. Thick line rendering via instanced quads

For line widths > 1 px, replace the `Lines` topology with instanced quad drawing.
Each line instance = 1 vertex, 4 quad corners generated in the vertex shader:

```glsl
// line_thick.vert  — gl_VertexIndex selects which quad corner
layout(location = 0) in vec4  inst_endpoints;  // x1,y1,x2,y2
layout(location = 1) in vec4  inst_color;
layout(location = 2) in float inst_half_width;  // in pixels

void main() {
    vec2 a = (world_to_clip * vec4(inst_endpoints.xy, 0, 1)).xy;
    vec2 b = (world_to_clip * vec4(inst_endpoints.zw, 0, 1)).xy;
    vec2 dir   = normalize(b - a);
    vec2 perp  = vec2(-dir.y, dir.x) * inst_half_width * pixel_to_ndc;

    // gl_VertexIndex 0,1,2,3 → quad corners
    vec2 corners[4] = {a - perp, a + perp, b - perp, b + perp};
    gl_Position = vec4(corners[gl_VertexIndex % 4], 0, 1);
    v_color = inst_color;
}
```

- Index buffer: `{0,1,2, 2,1,3}` (two triangles), used with `drawIndexed`.
- Instance count = number of lines; `drawIndexed(6, line_count)`.
- Works on all RHI backends (no geometry shader needed; those are unavailable on Metal).

#### 3c. LOD (Level of Detail)

At high zoom-out, many lines are sub-pixel (< 0.5 px on screen) and invisible.
Skip them before adding to the vertex buffer:

```cpp
// in rhi_renderer::draw_line():
double screen_len = (m_transform(end) - m_transform(start)).magnitude();
if (screen_len < 0.4)
    return;  // sub-pixel, skip entirely
```

For uniform grids at extreme zoom-out, an additional mip-level approach (pre-built
coarser grids) can reduce the visible set from 2 M to ~100 K.

---

### Phase 4 — Platform Hardening (~3 days)

**Goal:** solid CI on all three platforms with graceful fallback.

Steps:
1. **Runtime backend selection:**

```cpp
QRhi::Implementation selectBackend() {
#if defined(Q_OS_MACOS) || defined(Q_OS_IOS)
    return QRhi::Metal;
#elif defined(Q_OS_WIN)
    return QRhi::D3D12;
#else
    return QRhi::Vulkan;   // Linux default
#endif
}
```

Qt RHI handles the actual initialization; if `Vulkan` init fails (e.g., no GPU driver),
`QRhiWidget` emits `renderFailed()`. Connect this signal to fall back to
`deferred_renderer`:

```cpp
connect(m_rhi_widget, &QRhiWidget::renderFailed, this, [this]() {
    m_rhi_enabled = false;
    m_rhi_widget->hide();
    m_drawing_area_widget->show();
    redraw();   // re-renders via deferred_renderer
});
```

2. **Minimum Qt version guard** in `CMakeLists.txt`:

```cmake
if(Qt6_VERSION VERSION_GREATER_EQUAL "6.7.0")
    set(EZGL_RHI_AVAILABLE TRUE)
endif()

if(EZGL_RHI AND EZGL_RHI_AVAILABLE)
    target_compile_definitions(ezgl PRIVATE EZGL_RHI)
    target_link_libraries(ezgl PRIVATE Qt6::RhiWidgets Qt6::ShaderTools)
    target_sources(ezgl PRIVATE ...)
endif()
```

3. **Benchmark comparison:** add a `--backend` flag to `renderer-stress-bench`:
   `--backend rhi` / `--backend painter`. Print FPS + frame time for each.

4. **Export test:** `print_pdf`, `print_svg`, `print_png` always use
   `deferred_renderer` on a `QImage` (unchanged) — no RHI involved.

---

## 10. File Changes Summary

| Action | File | Notes |
|---|---|---|
| **new** | `include/ezgl/qt/rhi_canvas_widget.hpp` | `RhiCanvasWidget` declaration |
| **new** | `src/qt/rhi_canvas_widget.cpp` | `initialize()`, `render()` |
| **new** | `include/ezgl/qt/rhi_renderer.hpp` | `rhi_renderer` declaration |
| **new** | `src/qt/rhi_renderer.cpp` | Vertex collection, `flush()` |
| **new** | `include/ezgl/spatial_grid.hpp` | Spatial index |
| **new** | `src/spatial_grid.cpp` | Grid build + query |
| **new** | `shaders/line.vert` | Thin line vertex shader |
| **new** | `shaders/line.frag` | Flat color fragment shader |
| **new** | `shaders/line_thick.vert` | Instanced quad vertex shader (Phase 3b) |
| **modify** | `src/canvas.cpp` | `#ifdef EZGL_RHI` branch in `initialize()` + `redraw()` |
| **modify** | `CMakeLists.txt` | `qt6_add_shaders`, new sources, `Qt6::RhiWidgets` |
| no change | `include/ezgl/graphics.hpp` | Public API unchanged |
| no change | `src/graphics.cpp` | Immediate-mode renderer unchanged |
| no change | `include/ezgl/qt/deferred_renderer.hpp` | Still used as fallback and for export |
| no change | `src/qt/deferred_renderer.cpp` | Still used as fallback and for export |

---

## 11. Performance Targets

| Scenario | QPainter baseline | deferred_renderer | RHI target |
|---|---|---|---|
| 1 M lines, static camera | ~1 fps | ~30 fps | 200+ fps |
| 10 M lines, static camera | ~0.1 fps | ~5 fps | 60+ fps |
| 100 M lines (with spatial culling, 2 M visible) | — | ~2 fps | 60+ fps |
| Pan/zoom (only UBO update, no re-upload) | slow | slow | 200+ fps |

The 60 fps at 100 M total lines is achievable because:
- Spatial culling limits GPU vertex count to ~2 M per frame (at 1920×1080).
- 2 M lines × 24 bytes = 48 MB upload per frame; PCIe bandwidth is ~10 GB/s.
- GPU draws 2 M lines at ~2 ns/line = 4 ms → well within a 16 ms frame budget.
- Static-camera optimization (no re-upload) reduces frame time to ~0.5 ms.

---

## 12. Risk Notes

- **QRhiWidget requires Qt 6.7+.** If the project must support older Qt, use
  `QWindow + QWidget::createWindowContainer()` with manual swapchain management.
  Increase complexity significantly; consider it Phase 5.
- **Metal line topology**: Metal supports `MTLPrimitiveTypeLine` but discards
  `gl_PointSize` and does not support `gl_LineWidth`. Thick lines **must** use
  instanced quads (Phase 3b) on macOS — there is no Metal equivalent of OpenGL
  `glLineWidth`.
- **QPainter overlay compositing**: `grabFramebuffer()` on `QRhiWidget` is
  synchronous and requires a GPU readback. It's fine for sparse overlay
  (text, a few arcs) but becomes a bottleneck if the scene has many polygons
  or arcs. Profile and move more primitives to GPU if needed.
- **Painter's-algorithm ordering** between GPU and QPainter overlay: GPU
  primitives (lines, rects) are always drawn first; QPainter overlay (text,
  arcs) is always on top. For VTR this is correct by convention. If a caller
  expects interleaved ordering (e.g., a label behind a line), a sort pass would
  be needed.
- **Thread safety**: `RhiCanvasWidget::render()` runs on the Qt render thread.
  `rhi_renderer::flush()` runs on the main thread. The `m_frame_mutex` in
  `RhiCanvasWidget` guards the `m_pending_*` vectors.
