# RHI Renderer Reimplementation Design

## 1. Motivation

The current `rhi_renderer` is tile-based and functional, but its internal
organisation has several friction points that grow worse as the primitive set
expands:

| Pain point | Root cause |
|---|---|
| Per-vertex style float in every line/rect stream | Global palette forces a per-vertex index; wastes 4 bytes/vertex for a value that is constant across an entire batch |
| Filled rects upload 6 `PosVertex` (48 B) each | Triangle-list expansion done on the CPU; corners could be reconstructed in the vertex shader |
| `RhiTileBatch` is a monolithic struct with 10+ parallel vectors | Adding a new primitive type requires editing the struct, `RhiCanvasWidget`, and all upload paths simultaneously |
| Tile-first nesting causes redundant SRB rebinds | Every tile rebinds the pipeline and palette UBO for every primitive type, even when the style has not changed |

The redesign flips the primary axis from **tile → style** to **style → chunk**:
each primitive type owns a flat per-style buffer covering the whole scene; that
buffer is subdivided into spatial *chunks* (one per occupied tile cell) for
visibility culling. Style is delivered to the shader via a per-style UBO — no
per-vertex or per-instance style data, no global palette.

The tile spatial culling, the QPainter overlay path, and the
`flush_mvp_only()` camera-only update are preserved unchanged.

---

## 2. Core Concepts

### 2.1 PrimitiveTypes enum

```cpp
enum class PrimitiveType : std::uint8_t {
    ThinLine,       // draw_line, draw_rectangle outlines — Lines topology
    FilledRect,     // fill_rectangle — instanced TriangleStrip, 2 corners
    FilledPoly,     // fill_polygon (triangulated) — Triangle topology
    ThickLine,      // line_width > 0 — instanced TriangleStrip
    DashedLine,     // dashed style — instanced TriangleStrip
};
```

### 2.2 Style and per-type key

```cpp
struct Style {
    color fill_color;
    color line_color;
    float line_width_px = 0.f;
    float dash_px       = 0.f;
    float gap_px        = 0.f;

    // Returns a compact key unique within the given primitive type.
    // Different types care about different fields: two Style objects that differ
    // only in fill_color return the same ThinLine key, so they share one buffer.
    std::uint32_t calc_key(PrimitiveType pt) const noexcept {
        switch (pt) {
        case PrimitiveType::ThinLine:
            return pack_color(line_color);
        case PrimitiveType::FilledRect:
        case PrimitiveType::FilledPoly:
            return pack_color(fill_color);
        case PrimitiveType::ThickLine:
            return pack_color(line_color)
                 ^ (std::uint32_t(line_width_px * 4) << 24);
        case PrimitiveType::DashedLine:
            return pack_color(line_color)
                 ^ (std::uint32_t(line_width_px * 4) << 24)
                 ^ (std::uint32_t(dash_px) << 16)
                 ^ (std::uint32_t(gap_px)  <<  8);
        }
    }
};
```

### 2.3 Spatial chunk

A `Chunk` is a contiguous sub-range of a style buffer's vertex/instance array
that belongs to one tile cell. It carries the tile's world bounds so that
`render()` can skip it without touching the vertex data.

```cpp
struct Chunk {
    rectangle     world_bounds;  // tile cell bounds — used for visibility test
    std::uint32_t offset;        // first vertex/instance index in the flat array
    std::uint32_t count;         // number of vertices/instances
};
```

### 2.4 Per-style buffer hierarchy

Each concrete buffer covers one (primitive type, style key) pair for the entire
scene. Geometry is stored flat; `chunks` records where each tile cell's data
starts and ends.

```cpp
// Base — common chunk tracking, no geometry store.
struct StyleBuffer {
    std::uint32_t       style_key;   // Style::calc_key result
    std::uint32_t       rgba;        // packed RGBA written to the style UBO
    std::vector<Chunk>  chunks;      // spatial sub-ranges, one per occupied tile

    virtual bool empty() const noexcept = 0;
    virtual void clear() noexcept = 0;
    virtual ~StyleBuffer() = default;
};

// ---- Concrete buffers -------------------------------------------------------

struct ThinLineStyleBuffer : StyleBuffer {
    // Flat list of all line vertices for this style, ordered by chunk.
    // No per-vertex style data — color comes from the per-style UBO.
    std::vector<PosVertex> verts;

    bool empty() const noexcept override { return verts.empty(); }
    void clear() noexcept override { chunks.clear(); verts.clear(); }
};

struct FillRectStyleBuffer : StyleBuffer {
    // 2-corner instanced: 16 B per rect vs 48 B for 6 expanded vertices.
    struct RectInstance { float x0, y0, x1, y1; };  // world-space corners
    static_assert(sizeof(RectInstance) == 16);

    std::vector<RectInstance> instances;

    bool empty() const noexcept override { return instances.empty(); }
    void clear() noexcept override { chunks.clear(); instances.clear(); }
};

struct FillPolyStyleBuffer : StyleBuffer {
    // Triangulated vertices. No per-vertex style — color from UBO.
    // (Style-first grouping eliminates the per-vertex palette lookup that the
    // previous design required when polygons of mixed styles shared one buffer.)
    std::vector<PosVertex> verts;

    bool empty() const noexcept override { return verts.empty(); }
    void clear() noexcept override { chunks.clear(); verts.clear(); }
};

struct ThickLineStyleBuffer : StyleBuffer {
    // Per-instance data unchanged from current ThickLineInstance.
    // Parallel style array removed — color comes from UBO.
    std::vector<ThickLineInstance> instances;

    bool empty() const noexcept override { return instances.empty(); }
    void clear() noexcept override { chunks.clear(); instances.clear(); }
};

struct DashedLineStyleBuffer : StyleBuffer {
    std::vector<DashedLineInstance> instances;

    bool empty() const noexcept override { return instances.empty(); }
    void clear() noexcept override { chunks.clear(); instances.clear(); }
};
```

### 2.5 Scene-level buffer collection

`RhiTileBatch` is replaced by a flat collection of per-style buffers, one map
per primitive type. The tile grid is retained internally in `rhi_renderer` for
building chunk boundaries, but is no longer the primary data structure seen by
`RhiCanvasWidget`.

```cpp
struct SceneBuffers {
    std::unordered_map<std::uint32_t, ThinLineStyleBuffer>   thin_lines;
    std::unordered_map<std::uint32_t, FillRectStyleBuffer>   fill_rects;
    std::unordered_map<std::uint32_t, FillPolyStyleBuffer>   fill_polys;
    std::unordered_map<std::uint32_t, ThickLineStyleBuffer>  thick_lines;
    std::unordered_map<std::uint32_t, DashedLineStyleBuffer> dashed_lines;

    bool empty() const noexcept {
        return thin_lines.empty() && fill_rects.empty()
            && fill_polys.empty() && thick_lines.empty()
            && dashed_lines.empty();
    }
    void clear() noexcept {
        thin_lines.clear(); fill_rects.clear(); fill_polys.clear();
        thick_lines.clear(); dashed_lines.clear();
    }
};
```

---

## 3. Style Delivery via Per-Style UBO

### Current mechanism (global palette + per-vertex index)

```
CPU:  write inStyleNorm = idx / 255.f  per vertex/instance
GPU:  frag: palette_UBO.colors[int(inStyleNorm * 255 + 0.5)]
```

Problems:
- `inStyleNorm` occupies 4 bytes per vertex as a float attribute.
- The 4096-byte palette UBO is uploaded even when only 2–3 colors are used.
- Fragment shader has an indirect read through the palette; color is not a
  compile-time-constant uniform register.
- Every tile rebinds the palette UBO and pipeline state even when the style
  is the same as the previous tile.

### New mechanism — per-style UBO

Each style buffer maps to exactly one UBO value (`vec4 color`). That value is
bound once before iterating the buffer's visible chunks. All vertex shaders
lose the `inStyleNorm` attribute entirely; all fragment shaders reduce to:

```glsl
// binding 1 — 16 bytes, replaced per style
layout(std140, binding = 1) uniform style_buf {
    vec4 color;
} style;

void main() { fragColor = style.color; }
```

CPU render loop:

```
for each ThinLineStyleBuffer in scene.thin_lines:
    update style_ubuf { color = unpack(buf.rgba) }  // one UBO write per style
    bind SRB (mvp_ubuf @ 0, style_ubuf @ 1)         // one bind per style
    for each Chunk in buf.chunks:
        if chunk.world_bounds.intersects(visible_world):  // CPU visibility test
            setVertexInput(vbo, offset = chunk.offset * sizeof(PosVertex))
            cmdDraw(chunk.count)                           // draw call per visible chunk
```

Benefits over both the old palette design and the earlier tile-first per-batch
UBO proposal:

| | Old (palette) | Tile-first per-batch UBO | Style-first per-style UBO |
|---|---|---|---|
| SRB rebind rate | per tile × type | per tile × style × type | **per style × type** |
| Per-vertex style bytes | 4 B | 0 B | **0 B** |
| Global palette UBO | 4096 B always | gone | **gone** |
| Fragment shader reads | indirect palette | direct | **direct** |
| GPU VBO access pattern | per-tile scatter | per-tile scatter | **contiguous per style** |

The style-first layout also means consecutive draw calls for the same style's
chunks share the same pipeline binding, which lets the driver/GPU coalesce them
more aggressively.

---

## 4. GPU Data Minimisation

### 4.1 Filled rectangles — 2-corner instancing

**Current**: 6 `PosVertex` (48 B) per rect — triangle-list expansion on CPU.
**Proposed**: 1 `RectInstance` (16 B) — vertex shader reconstructs 4 corners
from `gl_VertexIndex`. **3× less bandwidth.** Rects are the most common
primitive in VPR.

```glsl
// fill_rect.vert — instanced TriangleStrip, 4 vertices per instance
layout(location = 0) in vec2 inMin;  // per-instance world-space min corner
layout(location = 1) in vec2 inMax;  // per-instance world-space max corner
// No inColor — comes from per-style UBO at binding 1.

void main() {
    float x = ((gl_VertexIndex & 1) == 0) ? inMin.x : inMax.x;
    float y = ((gl_VertexIndex & 2) == 0) ? inMin.y : inMax.y;
    gl_Position = ubo.mvp * vec4(x, y, 0.0, 1.0);
}
```

Draw call: `drawInstanced(4, chunk.count)` with TriangleStrip per chunk.

`RectInstance` is 16 bytes flat — no embedded color, no parallel style array.
The per-style UBO provides the color for the entire draw.

### 4.2 Thin line vertices — drop style attribute

With per-style UBO the `inStyleNorm` float attribute is eliminated. `PosVertex`
remains 8 bytes; no structural change, but the parallel style VBO stream is
gone. For 100k thin-line vertices this saves 400 kB of VBO bandwidth per frame.

### 4.3 Thick and dashed line instances — parallel style array removed

Current `ThickLineInstance` (20 B) and `DashedLineInstance` (32 B) already
carry no color — they used a parallel `StyleIndex` VBO. With the per-style
design that VBO is eliminated without changing the instance structs at all.
The style UBO provides the color for the entire draw.

### 4.4 Filled polygon vertices — per-vertex style eliminated

The previous design kept per-vertex style for polygons because grouping
triangulation output by style post-hoc was expensive. With style-first buffers
this problem does not arise: `rhi_renderer` writes polygon vertices directly
into the correct `FillPolyStyleBuffer` (keyed by fill color) as it
triangulates. No sorting is needed; no per-vertex style attribute is needed.
`PosVertex` (8 B) is the full vertex for polygon geometry.

---

## 5. How Chunks Are Built (rhi_renderer side)

The tile grid (`kTileGridDimension = 256`, scene bounds, `m_tile_width/height`)
is retained inside `rhi_renderer` as the spatial indexing tool for building
chunks. It is no longer visible to `RhiCanvasWidget`.

For each primitive call (e.g. `draw_line`):

1. Compute `style_key = Style::calc_key(ThinLine)` from the current renderer
   state.
2. Look up or create the `ThinLineStyleBuffer` in `m_scene.thin_lines`.
3. Clip the segment to tile cells (same logic as today).
4. For each occupied tile cell `(tx, ty)`:
   - If no chunk exists for `(tx, ty)` in this style buffer, open a new one:
     record `Chunk { tile_world_bounds, offset = verts.size(), count = 0 }`.
   - Append the clipped vertices to `buf.verts`; increment `chunk.count`.

At `flush()` time the flat `verts` array is complete; `chunks` already contains
the correct `(offset, count)` ranges. No post-sorting or re-packing is needed.

A lightweight per-style tile index (e.g. a flat `uint32_t` array sized
`cols × rows` initialised to `kInvalidIndex`) tracks which chunks exist,
enabling O(1) lookup per tile cell during build.

---

## 6. RhiCanvasWidget Upload Changes

### 6.1 FrameResources simplification

The ~14 parallel `QRhiBuffer` vector members collapse to one set of streaming
VBO pools, one per primitive type:

```cpp
struct FrameResources {
    std::unique_ptr<QRhiBuffer>              mvp_ubuf;       // binding 0, 80 B
    std::unique_ptr<QRhiBuffer>              style_ubuf;     // binding 1, 16 B (updated per style)

    // Streaming geometry pools — grow as needed, shared across all styles.
    std::vector<std::unique_ptr<QRhiBuffer>> thin_line_vbufs;
    std::vector<std::unique_ptr<QRhiBuffer>> fill_rect_inst_vbufs;
    std::vector<std::unique_ptr<QRhiBuffer>> fill_poly_vbufs;
    std::vector<std::unique_ptr<QRhiBuffer>> thick_line_inst_vbufs;
    std::vector<std::unique_ptr<QRhiBuffer>> dashed_line_inst_vbufs;

    // Overlay (unchanged)
    std::unique_ptr<QRhiTexture>             overlay_tex;
    std::unique_ptr<QRhiShaderResourceBindings> srb;
    // ...
};
```

Removed entirely: `line_style_vbufs`, `fill_style_vbufs`, `draw_vbufs`,
`draw_style_vbufs`, `fill_poly_style_vbufs`, `thick_line_style_vbufs`,
`dashed_line_style_vbufs`, `palette_ubuf`, `line_color_ubuf`.

The `set_frame_data()` interface changes from `vector<RhiTileBatch>` +
`palette_rgba` to `SceneBuffers` (the style-keyed maps).

### 6.2 Render loop structure

```
// --- Thin lines ---
bind thin_line_pso
for each (key, buf) in scene.thin_lines:
    update style_ubuf { color = unpack(buf.rgba) }
    bind SRB
    for each chunk in buf.chunks:
        if chunk.world_bounds ∩ visible_world ≠ ∅:
            setVertexInput(thin_line_vbuf, chunk.offset * 8)
            cmdDraw(chunk.count)

// --- Filled rects ---
bind fill_rect_pso
for each (key, buf) in scene.fill_rects:
    update style_ubuf { color = unpack(buf.rgba) }
    bind SRB
    for each chunk in buf.chunks:
        if visible:
            setVertexInput(fill_rect_inst_vbuf, chunk.offset * 16)
            cmdDraw(4, chunk.count)   // 4 corners × N instances

// --- Filled polygons ---
bind fill_poly_pso
for each (key, buf) in scene.fill_polys:
    update style_ubuf { color = unpack(buf.rgba) }
    bind SRB
    for each chunk in buf.chunks:
        if visible:
            setVertexInput(fill_poly_vbuf, chunk.offset * 8)
            cmdDraw(chunk.count)

// --- Thick lines ---
bind thick_line_pso
for each (key, buf) in scene.thick_lines:
    update style_ubuf { color = unpack(buf.rgba) }
    bind SRB
    for each chunk in buf.chunks:
        if visible:
            setVertexInput(thick_line_corner_vbuf || inst_vbuf, ...)
            cmdDraw(4, chunk.count)

// --- Dashed lines ---
bind dashed_line_pso
for each (key, buf) in scene.dashed_lines:
    update style_ubuf { color = unpack(buf.rgba) }
    bind SRB
    for each chunk in buf.chunks:
        if visible:
            cmdDraw(4, chunk.count)

// --- Overlay ---
bind overlay_pso; draw quad
```

The PSO is bound **once per primitive type** for the entire frame. The SRB is
updated **once per style**. `setVertexInput + cmdDraw` pairs are emitted only
for visible chunks. This is the minimum possible state-change rate.

---

## 7. Spatial Culling — What Happens to Non-Visible Chunks

The culling model is unchanged from the current design: non-visible geometry
**never reaches the GPU vertex shader**. The difference is that culling now
happens at chunk granularity within a style buffer rather than at tile
granularity within a tile batch.

For a non-visible chunk:
- `chunk.world_bounds.intersects(visible_world)` is false on the CPU.
- No `cmdDraw()` is emitted for it.
- Its bytes sit in VRAM (inside the style buffer's VBO) but are never read
  by the GPU for that frame.
- The vertex shader never runs on a single one of its vertices.

On a camera-only update (`flush_mvp_only()`):
- The style buffer geometry VBOs are unchanged in VRAM.
- Only `mvp_ubuf` is updated (80 bytes).
- The render loop re-evaluates chunk visibility against the new `visible_world`
  and emits draw calls only for now-visible chunks. Zero geometry re-upload.

---

## 8. Shader Changes Summary

| Shader | Change |
|---|---|
| `line.vert` | Remove `inStyleNorm` attribute — no other change |
| `line.frag` | Replace `palette_buf { vec4 colors[256]; }` with `style_buf { vec4 color; }` |
| `line_single_style.vert/frag` | **Delete** — subsumed; per-style UBO is always optimal |
| `fill_rect.vert` | **New** — instanced: `inMin vec2`, `inMax vec2`; reconstruct corner from `gl_VertexIndex`; no `inColor` |
| `fill_rect.frag` | **New** — trivial: `fragColor = style.color` |
| `thick_line.vert` | Remove `inStyleNorm` — no other change |
| `thick_line.frag` | Replace palette lookup with `fragColor = style.color` |
| `dashed_line.vert` | Remove `inStyleNorm` — no other change |
| `dashed_line.frag` | Replace palette lookup with `fragColor = style.color` |

No shader gains per-vertex or per-instance color data. All color comes from
`style_buf` at binding 1.

---

## 9. Migration Path (Incremental)

Each step is independent; roll back is safe at any boundary.

1. **`FillRectStyleBuffer` + instanced rect shader** — highest data reduction
   (48 B → 16 B per rect), self-contained. Add `fill_rect_pso` alongside the
   existing `fill_pso`; route `fill_rectangle` to the new path; remove old path
   when stable.

2. **`ThinLineStyleBuffer` + style UBO** — removes the `inStyleNorm` attribute
   and the parallel style VBO for the highest-vertex-count stream. Add
   `ThinLineStyleBuffer` build path; keep old path as fallback; switch and
   remove `line_single_style_pso` when confirmed correct.

3. **`FillPolyStyleBuffer`** — same pattern; removes per-vertex style from
   polygons. Low risk because polygons are uncommon in typical VPR scenes.

4. **`ThickLineStyleBuffer` / `DashedLineStyleBuffer`** — remove parallel style
   VBOs; update shaders. Instance structs are unchanged in size.

5. **`SceneBuffers` replaces `RhiTileBatch` in the API** — final step once all
   four buffer types are migrated. Remove `palette_ubuf`, `line_color_ubuf`, and
   all style VBO vectors from `FrameResources`.

---

## 10. Summary of Recommendations

| Decision | Recommendation | Rationale |
|---|---|---|
| Style-first buffer organisation | **Yes** | Fewer SRB rebinds; contiguous GPU memory per style; cleaner extensibility |
| Per-style UBO `{vec4 color}` | **Yes, for all types** | No per-vertex/instance color data; no palette; trivial fragment shader |
| Rect instancing (2 corners, 16 B) | **Strongly yes** | 3× less rect data; rects dominate VPR scenes |
| Per-vertex style for polygons | **Remove** | Style-first grouping makes it unnecessary; triangulation output goes directly into the right buffer |
| Spatial chunks within style buffers | **Yes** | Preserves tile-granularity CPU culling without `RhiTileBatch` |
| Delete `line_single_style_pso` | **Yes** | Per-style UBO is always optimal for the single-style case |
| Keep tile grid inside `rhi_renderer` | **Yes** | Still needed to compute chunk boundaries during scene build |
| Keep `flush_mvp_only()` | **Yes** | Camera pan/zoom re-evaluates chunk visibility; zero geometry re-upload |
| Keep overlay / `deferred_renderer` | **Yes** | QPainter overlay is the right tool for text/arcs; unchanged |
