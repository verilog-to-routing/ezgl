# ezgl Renderer Backends

ezgl ships three backends behind the `ezgl::renderer` API. They are
selected at runtime via VPR's `--renderer {immediate|deferred|rhi}` flag
(VPR sets the canvas via `canvas::set_renderer_type(...)`; see
[`include/ezgl/qt/render_backend.hpp`](../include/ezgl/qt/render_backend.hpp)).
The **default is `rhi`**; the other two are available for compatibility
and fallback. All three render the same scenes; they differ only in
*how* the primitives reach the screen.

---

## immediate

The baseline. Every `draw_line` / `fill_rectangle` / `draw_text` call
forwards straight to a single QPainter call on a `QImage` backing store.
There is no scene graph, no spatial index, no GPU. The whole frame is
re-painted on every screen update by replaying the draw callback against
the QPainter — same model as the original GTK/Cairo path the Qt port
replaced.

**Pros**
- Simplest path; correct by construction (whatever QPainter renders is
  what you get).
- No setup cost — first frame is on screen as fast as the draw callback
  can run.
- Works under every Qt QPA platform (X11, Wayland, offscreen, etc.).
- No GPU drivers, no shader compilation, no build-tool dependencies
  beyond Qt itself.

**Cons**
- One QPainter call per primitive — per-call overhead dominates at high
  primitive counts.
- CPU-bound; no GPU rasterization, no hardware AA.
- Pan/zoom redraws the entire scene from scratch even when nothing
  visible changed — no camera-only cache.

**Use when** the scene is small, or for renderer A/B comparison and
regression baselining (VTR's Layer 5 visual tests use it as one of
three rendered targets).

---

## deferred

Same QPainter rasterizer underneath, but every primitive emitted during
the draw callback is recorded into per-type / per-style batches instead
of dispatching immediately. At `flush()` time the batches replay through
QPainter's bulk APIs (`drawLines(QLineF*, n)`, `drawRects(QRectF*, n)`,
…), setting the pen/brush once per batch instead of once per primitive.
A per-frame spatial index over the recorded overlay primitives also lets
the replay cull off-screen ones cheaply.

**Pros**
- Faster than immediate on FPGA-scale scenes (thousands of nets,
  routed wires).
- No GPU dependency; runs everywhere QPainter does.

**Cons**
- Higher memory: the entire scene's primitive list lives in RAM between
  the record phase and `flush()`.
- More complex than immediate: per-style batching, painter-state capture
  per primitive, spatial-index maintenance, and a small overlay queue
  for primitives whose camera-dependent extent can't be tightly bounded
  at record time.
- Still CPU rasterized — the QPainter rasterizer is the ceiling. At
  ~10⁸ primitives even batched calls saturate the CPU.

**Use when** the scene is large enough that immediate stutters but the
target platform doesn't have (or can't be trusted to have) a working
GPU/RHI.

---

## rhi

Renders through Qt's `QRhi` GPU abstraction — Vulkan on Linux, Direct3D
12 on Windows, Metal on macOS — via `QRhiWidget` for on-screen display
and an offscreen `QRhi` swapchain for `render_to_image()` (used by
`save_graphics` and the headless regression tests). Shaders are compiled
from GLSL to `.qsb` by `qsb` at build time (see the project README) and
loaded from the Qt resource system. The CPU records vertex/index buffers
once per scene change, then the GPU rasterizes — removing the CPU
rasterization bottleneck of the QPainter-based paths.

**Pros**
- GPU-rasterized — scales to massive scenes at interactive frame rates.
- Hardware 4× MSAA — thin diagonal lines and circle arcs are smooth
  without CPU AA cost. Same sample count on-screen and in the offscreen
  `render_to_image()` path (the offscreen path resolves to a single-sample
  texture before readback so PNGs come out as you'd see them on screen).
- Pan/zoom is a single MVP-matrix upload, not a re-rasterization.

**Cons**
- `QRhiWidget` cannot acquire a QRhi under `QT_QPA_PLATFORM=offscreen`,
  so VPR auto-falls back to `immediate` when `--disp on` is combined
  with offscreen QPA (see `EZGL: active renderer backend: ...` log line
  for what actually got installed). Headless `--disp off` is fine —
  `render_to_image()` creates an offscreen QRhi directly.
- Some QPainter-only primitives (text, arcs, surfaces) still route
  through a CPU-rendered overlay layer (an internal `deferred_renderer`
  painting into a QImage that's composited over the GPU frame) — those
  paths inherit deferred-mode cost.
- Driver-dependent: Vulkan/D3D12/Metal behavior is mostly portable, but
  GLSL variants and pipeline state differ per driver; the build bakes
  multiple GLSL versions (`100es,120,150,330`) plus HLSL 50 and MSL 12
  to maximize coverage.

**Use when** the scene is too large for deferred to keep interactive,
or when accurate hardware-AA line rendering matters (routed-net
overviews, congestion maps over the full RR graph).
