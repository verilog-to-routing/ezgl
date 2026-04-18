# Renderer Subsystem Refactor — Requirements

## 1. Background

The current renderer subsystem has three classes arranged in a single inheritance chain:

```
renderer          (graphics.hpp)      — public API + immediate QPainter implementation
  └── deferred_renderer               — batching / deferred QPainter path
        └── rhi_renderer              — GPU path via Qt RHI
```

`renderer` serves a dual role: it defines the public drawing API consumed by VPR callbacks **and** provides an immediate-mode QPainter implementation. `deferred_renderer` and `rhi_renderer` inherit concrete state and helpers from it rather than programming to an interface. This coupling makes it impossible to select a renderer at runtime without carrying the full `renderer` implementation, and keeps both concrete renderers in the type hierarchy simultaneously.

The goal of this refactor is to cleanly separate the interface from its implementations and allow VPR to select the active renderer at runtime. The immediate-mode renderer is retained as a concrete implementation for testing purposes, but its logic is fully isolated from `deferred_renderer` and `rhi_renderer`.

---

## 2. Requirements

### R1 — Preserve the public `ezgl::renderer` API

The set of public methods on `ezgl::renderer` **must not change**. All VPR draw callbacks receive a `renderer*` and call methods such as:

- `set_color`, `set_line_width`, `set_line_cap`, `set_line_dash`
- `set_font_size`, `format_font`, `set_text_rotation`
- `set_horiz_justification`, `set_vert_justification`
- `set_coordinate_system`, `set_visible_world`
- `get_visible_world`, `get_visible_screen`, `world_to_screen`
- `draw_line`, `draw_rectangle`, `fill_rectangle`
- `fill_poly`, `draw_elliptic_arc`, `fill_elliptic_arc`, `draw_arc`, `fill_arc`
- `draw_text`, `draw_surface`
- `load_png`, `free_surface`

No caller-visible signature, name, or semantic may change. This requirement is non-negotiable.

### R2 — Decouple the immediate-mode renderer from `deferred_renderer` and `rhi_renderer`

The immediate-mode QPainter implementation (synchronous draw calls against a `Painter*`) must be isolated into its own concrete class that inherits directly from `irenderer`. It is **not** removed — it is retained for testing and debugging purposes — but it must no longer be a base class for `deferred_renderer` or `rhi_renderer`.

Any helpers currently shared through the inheritance chain (coordinate transforms, clipping utilities, painter state fields) must be moved to a non-public internal utility layer accessible to all three concrete renderers independently, rather than inherited from `renderer`.

### R3 — Introduce `irenderer` as a pure abstract interface

A new class `ezgl::irenderer` must be introduced. It declares every drawing and state method as a pure virtual function. It has no data members and no implementation.

`ezgl::renderer` becomes a `final` concrete class that inherits from `irenderer` and provides the immediate-mode QPainter implementation. It is no longer a base class for any other renderer. The name `renderer` and its full public API (see R1) are preserved unchanged.

### R4 — All three renderers inherit directly from `irenderer`

```
irenderer                  (pure interface)
  ├── renderer       final (immediate QPainter — testing / fallback)
  ├── deferred_renderer    (QPainter batching path)
  └── rhi_renderer         (Qt RHI / GPU path)
```

No concrete renderer may inherit from another concrete renderer. Any code currently shared between `deferred_renderer` and `rhi_renderer` via the old `deferred_renderer → rhi_renderer` chain must be moved to a standalone internal utility class or duplicated if the sharing was incidental.

Each class implements the full `irenderer` interface independently.

### R6 — Off-screen culling must be owned by each concrete renderer independently

The immediate-mode `renderer` contains a two-layer culling mechanism that fires synchronously on every draw call. After the refactor this logic must not be inherited — each concrete renderer owns its culling strategy independently. For `deferred_renderer` in particular, culling must be applied at the correct points in its record/replay cycle.

#### Background: what the immediate renderer does today

`renderer::rectangle_off_screen()` performs a fast world-space AABB test against the current visible world rectangle. It is called at the top of every hot-path draw call (`draw_line`, `fill_rectangle`, `draw_rectangle`, `fill_poly`, `draw_elliptic_arc`, `draw_arc`, `draw_text`, `draw_surface`) and returns immediately if the primitive is entirely outside the viewport. This is a coarse, O(1) guard that prevents any further work for invisible geometry. After the refactor `renderer` keeps this mechanism internally — it is not inherited by other renderers.

#### What `deferred_renderer` must guarantee after the refactor

`deferred_renderer` operates in two phases — **record** (draw callback runs) and **replay** (`flush()` / `replay()` execute the stored commands). Off-screen culling must be applied at **replay time**, when the current camera state is known and the decision of which primitives enter the batch sent to QPainter is made.

**Hot-path primitives (lines, rectangles)**

During replay, before a stored line or rectangle is included in the visible batch, `deferred_renderer` must verify that it intersects the current screen viewport:

- *Lines*: a proper 2D segment-vs-viewport intersection test (equivalent to the current `screen_line_visible()`) must be performed per stored line, taking line width into account as a padding on the viewport bounds. A line whose AABB does not intersect the padded viewport is dropped from the batch.
- *Filled rectangles*: an AABB intersection test against the screen viewport (equivalent to `screen_rect_visible()`) must be applied per stored rect.
- *Outlined rectangles*: same as filled rectangles, with half the stroke width added as padding.

**Overlay commands (polygons, arcs, text, surfaces)**

Overlay commands are stored as `DeferredOverlayCommand` variants. At replay time, each candidate command must pass a visibility check before being dispatched to QPainter:

- *Polygons*: world-space or screen-space AABB of all vertices tested against the visible world or screen viewport respectively.
- *Arcs*: bounding rectangle `[center ± radius_x, center ± radius_y]` tested against the visible world or screen viewport.
- *Text*: the fit-box (accounting for justification offset) tested against the visible world or screen viewport; additionally, if the fit-box projects to fewer than a minimum number of pixels on screen the command is skipped as unreadable.
- *Surfaces*: bounding rectangle of the scaled image (accounting for justification offset) tested against the visible world or screen viewport.

**Spatial index for overlay commands**

The 256 × 256 spatial grid that indexes world-space overlay commands by their world bounding rectangle must be preserved. The replay loop queries it with the current visible world to obtain a candidate set before applying the per-command visibility checks above. This avoids iterating all stored overlay commands when only a small fraction of the scene is visible.

#### Key invariant

A primitive that is completely outside the current viewport must never reach the QPainter draw call during replay. The culling applied at replay time in `deferred_renderer` is semantically equivalent to the `rectangle_off_screen()` guard inside `renderer`, but it operates on the camera state at the moment of rendering rather than at the moment the draw callback ran. The two renderers arrive at the same result through independent implementations — not through shared code.

---

### R5 — Runtime renderer selection in VPR

VPR must select the active renderer at startup and hold a pointer of type `renderer*` (i.e., `irenderer*`) throughout its lifetime. The selection is made once, before the first draw, and does not change while the application is running.

- **Default:** `rhi_renderer` is used when Qt RHI is available (`EZGL_RHI` defined and GPU initialisation succeeds at runtime).
- **Fallback:** `deferred_renderer` is used when RHI is unavailable (Qt < 6.7, no GPU driver, or RHI init failure at runtime).
- The selection mechanism lives inside `ezgl::canvas`, not in VPR application code. VPR draw callbacks always receive `renderer*` and are unaware of which concrete type is active.

---

## 3. Non-Requirements

- No changes to the `ezgl::canvas` public API.
- No changes to VPR draw callback signatures or call sites.
- No changes to the shader pipeline or GPU data paths inside `rhi_renderer`.
- No changes to export paths (`print_pdf`, `print_svg`, `print_png`), which may continue to instantiate `deferred_renderer` internally on a scratch `QImage`.

---

## 4. Target Class Diagram

```
              «interface»
              irenderer
                + set_color(...)
                + set_line_width(...)
                + draw_line(...)
                + fill_rectangle(...)
                + fill_poly(...)
                + draw_text(...)
                + draw_surface(...)
                + ... (full public API)
         ▲            ▲            ▲
         │            │            │
   renderer      deferred_     rhi_renderer
   (final)       renderer      (Qt RHI / GPU)
 immediate        QPainter
 QPainter         batching
 (testing)
```

`renderer` is a `final` concrete class — it cannot be subclassed. `deferred_renderer` and `rhi_renderer` are also concrete and independent of each other. All three expose the same `irenderer*` interface to callers.

---

## 5. Migration Notes

- `canvas` currently holds a `std::unique_ptr<rhi_renderer>` under `#ifdef EZGL_RHI`. After the refactor it holds a `std::unique_ptr<irenderer>` regardless of build flags, and allocates the concrete type (`rhi_renderer`, `deferred_renderer`, or `renderer`) at runtime based on R5.
- The `#ifdef EZGL_RHI` guards remain in `CMakeLists.txt` and around `rhi_renderer`-specific includes. They do not affect `irenderer`, `renderer`, or `deferred_renderer`.
- Shared internal state currently inherited from `renderer` (painter pointer, camera pointer, color, line width, justification, coordinate system) must be replicated in each concrete renderer. A private internal `RendererState` struct is an acceptable mechanism as long as it is not part of the public `irenderer` interface.
- `renderer` must be marked `final` to make clear it is a leaf implementation and to allow the compiler to devirtualize calls when the concrete type is known statically (e.g., in tests that construct `renderer` directly).
