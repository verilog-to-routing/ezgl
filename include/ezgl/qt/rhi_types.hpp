#pragma once

#include "ezgl/rectangle.hpp"

#include <cstdint>
#include <unordered_map>
#include <vector>

/**
 * @file rhi_types.hpp
 *
 * @brief CPU-side data structures used by the rhi backend before GPU upload.
 *
 * The rhi backend never stores color per-vertex. Every primitive is grouped
 * by @ref ezgl::StyleKey (a packed 64-bit key combining primitive type,
 * RGBA, line width, and dash style); all geometry sharing one style is
 * uploaded into a single contiguous vertex/instance buffer and drawn with
 * one bind of the style UBO. This lets thousands of different-colored
 * lines render as a single per-primitive-type pipeline pass with no
 * per-color state churn.
 *
 * @par Why per-style UBO and not per-vertex color
 * Two alternatives were considered and rejected:
 *  - per-vertex @c uint8 RGBA: costs 4 B per vertex (large for 10⁸-scale
 *    line streams) and forces the same color to be written N times per
 *    style batch.
 *  - global palette UBO + per-vertex @c inStyleNorm index: forces a 4-byte
 *    float attribute per vertex, uploads a 4 KB palette every frame even
 *    when 2 colors are used, and adds an indirect read in the fragment
 *    shader.
 * Per-style UBO ({@c vec4 color}, 16 B per unique style) eliminates all of
 * the above: zero per-vertex/instance color bytes, no palette upload, and
 * the fragment shader collapses to @c fragColor=style.color (direct
 * register read, no indirect).
 *
 * @see ezgl::rhi_renderer for the recording side (tile binning + style
 *      bucket assignment).
 * @see ezgl::RhiSceneRenderer for the GPU upload side.
 *
 * @par Graphics terms used across the rhi/* files (glossary)
 * This is a student-oriented cheat sheet for the acronyms that recur in the
 * comments of every @c rhi_* file. Definitions are deliberately short; the
 * point is to make the other comments readable, not to be exhaustive.
 *
 *  - **GPU**   the graphics card/chip that draws pixels in parallel.
 *  - **CPU**   the main processor that runs our C++ and prepares data for the GPU.
 *  - **Qt RHI** Qt's "Rendering Hardware Interface": one API that Qt maps onto
 *              whatever the machine actually has (Vulkan, Metal, Direct3D, OpenGL).
 *              @c QRhi is the object representing that GPU connection.
 *  - **shader** a small program that runs on the GPU. A *vertex* shader positions
 *              each point; a *fragment* shader colors each pixel.
 *  - **vertex** one point of geometry (here: an @c (x,y) corner of a line/shape).
 *  - **VBO**   Vertex Buffer Object: a GPU array holding per-vertex (or, when
 *              *instanced*, per-shape) data.
 *  - **UBO**   Uniform Buffer Object: a small block of constants shared by every
 *              shader run in one draw (here it holds the color and the MVP matrix).
 *  - **SRB**   Shader Resource Bindings (Qt's @c QRhiShaderResourceBindings): the
 *              object that says "this UBO/texture is visible to the shader here."
 *  - **PSO**   Pipeline State Object (@c QRhiGraphicsPipeline): a bundle of fixed
 *              GPU settings + shader pair for one kind of primitive. Switching PSOs
 *              is costly, so we bind each one once per frame.
 *  - **instancing** drawing many copies of the same small shape (a line quad, an
 *              arrow triangle) from one set of vertices plus a per-copy record.
 *  - **MVP**   Model-View-Projection matrix: the transform that maps our world
 *              coordinates into what the GPU draws (see NDC).
 *  - **NDC**   Normalized Device Coordinates: the fixed square the GPU draws into,
 *              running -1..+1 in x and y regardless of window size.
 *  - **AABB**  Axis-Aligned Bounding Box: a rectangle (not rotated) around some
 *              geometry, used for cheap "is this visible?" tests.
 *  - **RGBA**  a color as red/green/blue/alpha (alpha = opacity), packed into 32 bits.
 *  - **VRAM**  the GPU's own memory. Plentiful, but slow to fill from the CPU...
 *  - **PCIe**  ...because CPU→GPU uploads travel over this bus, which is the
 *              expensive step we try to avoid re-doing every frame.
 *  - **MSAA**  Multi-Sample Anti-Aliasing: the GPU samples each pixel several times
 *              to smooth jagged edges, then averages ("resolves") them.
 *  - **DPR**   Device Pixel Ratio: physical pixels per logical pixel on hi-DPI
 *              screens (e.g. 2.0 on a "retina" display).
 *  - **swap chain** the small set of window-sized images the GPU rotates through:
 *              it draws the next frame into one while the screen shows another,
 *              then they swap. Being window-sized, it is rebuilt on every resize.
 *  - **QPA**   Qt Platform Abstraction: Qt's backend for a given platform. The
 *              special @c offscreen QPA has no window, used by headless tests.
 */

namespace ezgl {

// ---- Vertex / instance layouts ---------------------------------------------

/// World-space 2D position. Used as a per-vertex attribute for thin lines
/// and filled polygons. Color comes from the style UBO, not from the vertex.
struct PosVertex {
    float x, y;
};
static_assert(sizeof(PosVertex) == 8, "PosVertex must be 8 bytes");

/// One of the 4 quad corners shared by every thick-line / dashed-line
/// instance. @c t selects the line endpoint (0.0 = start, 1.0 = end) and
/// @c side selects which edge of the expanded quad (-1.0 = left, +1.0 =
/// right). The vertex shader uses these plus the instance's endpoints to
/// build a screen-space-thickness quad on the GPU.
struct QuadCorner {
    float t;
    float side;
};
static_assert(sizeof(QuadCorner) == 8, "QuadCorner must be 8 bytes");

/// One thick-line segment. The pixel width comes from the style UBO; the
/// vertex shader expands this into a 4-vertex quad in screen space (see
/// @c shaders/thick_line.vert) so width is invariant under zoom.
struct ThickLineInstance {
    float x0, y0;
    float x1, y1;
};
static_assert(sizeof(ThickLineInstance) == 16, "ThickLineInstance must be 16 bytes");

/// One dashed-line segment. @c phase_world carries the cumulative
/// world-space offset of this segment's start along the parent polyline so
/// the dash pattern stays continuous across the segment boundaries of one
/// logical polyline even when each segment is its own instance.
struct DashedLineInstance {
    float x0, y0;
    float x1, y1;
    float phase_world;
};
static_assert(sizeof(DashedLineInstance) == 20, "DashedLineInstance must be 20 bytes");

/// One axis-aligned filled rectangle, 16 B. The vertex shader
/// (TriangleStrip, 4-vertex instance) reconstructs each corner from
/// @c gl_VertexIndex against @c (x0,y0)-(x1,y1). The alternative
/// (6 expanded @ref PosVertex per rect = 48 B, triangle-list CPU
/// expansion) was 3× the bandwidth — significant because filled rects
/// dominate FPGA scenes (block fills, channel fills, congestion cells).
struct FillRectInstance {
    float x0, y0;
    float x1, y1;
};
static_assert(sizeof(FillRectInstance) == 16, "FillRectInstance must be 16 bytes");

/// One arrow head per instance: world anchor + world direction. Fixed-pixel
/// expansion to a 3-vertex triangle happens in the vertex shader so the
/// on-screen size never grows on zoom-in. Direction can be any nonzero
/// length — the shader normalises before computing the screen-space basis.
struct ArrowInstance {
    float ax, ay;
    float dx, dy;
};
static_assert(sizeof(ArrowInstance) == 16, "ArrowInstance must be 16 bytes");

// ---- Style key encoding ----------------------------------------------------

/// Packed 64-bit key identifying a unique render-state combination. Layout:
/// @code
///   bits  0–31 : rgba (uint32, packed in client byte order)
///   bits 32–47 : line_width_px (uint16, in pixels; 0 for fill primitives)
///   bits 48–55 : line_dash (uint8; 0 for solid)
///   bits 56–63 : PrimitiveType (uint8)
/// @endcode
/// Two primitives with the same key share one batch in @ref SceneBuffers
/// and one GPU draw call per chunk.
using StyleKey = std::uint64_t;

/// Render-state discriminator stored in the high byte (bits 56–63) of a
/// @ref StyleKey. Selects which GPU pipeline draws a primitive and which
/// per-type bucket of @ref SceneBuffers its geometry lands in; each
/// enumerator has a matching `*StyleBuffer` map there. The packed @c uint8_t
/// width keeps the key compact (see @ref pack_style_key).
enum class PrimitiveType : std::uint8_t {
    ThinLine,         ///< 1-pixel line; @ref PosVertex stream (@ref ThinLineStyleBuffer).
    FilledRect,       ///< Axis-aligned filled rectangle, GPU-instanced (@ref FillRectStyleBuffer).
    FilledPoly,       ///< Triangulated filled polygon; @ref PosVertex stream (@ref FillPolyStyleBuffer).
    ThickLine,        ///< Screen-width line expanded to a quad in the vertex shader (@ref ThickLineStyleBuffer).
    DashedLine,       ///< Thick line with a dash pattern; carries @c phase_world (@ref DashedLineStyleBuffer).
    Arrow,            ///< GPU-instanced arrow head; line_width field reused as arrow_size_px.
};

/// Pack a render-state tuple into a @ref StyleKey. See StyleKey for layout.
inline constexpr StyleKey pack_style_key(PrimitiveType primitive_type,
                                         std::uint32_t rgba,
                                         std::uint16_t line_width_px,
                                         std::uint8_t  line_dash) noexcept
{
    return StyleKey(rgba)
        | (StyleKey(line_width_px) << 32)
        | (StyleKey(line_dash)     << 48)
        | (StyleKey(std::uint8_t(primitive_type)) << 56);
}

/// Extract the line width (bits 32–47) from a @ref StyleKey, in pixels.
/// Zero for fill primitives; for @ref PrimitiveType::Arrow this field holds
/// the arrow size in pixels. Inverse of the @c line_width_px argument to
/// @ref pack_style_key.
inline constexpr std::uint16_t style_key_line_width(StyleKey key) noexcept
{
    return std::uint16_t((key >> 32) & 0xFFFFu);
}

/// Extract the dash style (bits 48–55) from a @ref StyleKey; 0 means solid.
/// Inverse of the @c line_dash argument to @ref pack_style_key.
inline constexpr std::uint8_t style_key_line_dash(StyleKey key) noexcept
{
    return std::uint8_t((key >> 48) & 0xFFu);
}

// ---- Scene buffer types (CPU-side geometry before GPU upload) --------------

/// A contiguous sub-range of a style buffer's vertex/instance array that
/// belongs to one tile cell. Carries the tile's world bounds so the GPU
/// draw loop can skip non-visible chunks without touching the vertex
/// data — coarse but cheap (one AABB test per chunk).
///
/// Non-visible chunks: the bytes stay in VRAM (the style's VBO is
/// uploaded whole) but no @c cmdDraw is emitted and the vertex shader
/// never runs on them. The alternative (per-frame partial vertex upload)
/// was rejected because VRAM is cheap, but PCIe re-upload is expensive.
struct Chunk {
    rectangle     world_bounds;   ///< Tile cell bounds — tested against the visible world rect.
    std::uint32_t offset = 0;     ///< First vertex/instance index in the flat style-buffer array.
    std::uint32_t count  = 0;     ///< Number of vertices/instances belonging to this tile cell.
};

/// Fields shared by every per-style buffer: the @ref StyleKey that uniquely
/// identifies this batch, the unpacked @c rgba used to fill the style UBO at
/// upload time (cached here so the GPU side need not re-decode the key), and
/// the per-tile @ref Chunk list that lets the draw loop cull non-visible
/// geometry. The concrete geometry array lives in each derived struct.
struct StyleBufferCommon {
    StyleKey           style_key = 0;
    std::uint32_t      rgba      = 0;
    std::vector<Chunk> chunks;
};

/// All thin (1-pixel) lines sharing one @ref StyleKey. Geometry is a flat
/// @ref PosVertex array (two verts per segment). @c chunks index sub-ranges
/// of @c verts. See @ref PrimitiveType::ThinLine.
struct ThinLineStyleBuffer : StyleBufferCommon {
    std::vector<PosVertex> verts;
    bool empty()  const noexcept { return verts.empty(); }
    void clear()        noexcept { chunks.clear(); verts.clear(); }
};

/// All filled rectangles sharing one @ref StyleKey, one @ref FillRectInstance
/// per rect (GPU-instanced). See @ref PrimitiveType::FilledRect.
struct FillRectStyleBuffer : StyleBufferCommon {
    std::vector<FillRectInstance> instances;
    bool empty()  const noexcept { return instances.empty(); }
    void clear()        noexcept { chunks.clear(); instances.clear(); }
};

/// All filled polygons sharing one @ref StyleKey, stored as a flat triangle
/// list of @ref PosVertex (CPU-triangulated). See @ref PrimitiveType::FilledPoly.
struct FillPolyStyleBuffer : StyleBufferCommon {
    std::vector<PosVertex> verts;
    bool empty()  const noexcept { return verts.empty(); }
    void clear()        noexcept { chunks.clear(); verts.clear(); }
};

/// All thick (screen-width) lines sharing one @ref StyleKey, one
/// @ref ThickLineInstance per segment. See @ref PrimitiveType::ThickLine.
struct ThickLineStyleBuffer : StyleBufferCommon {
    std::vector<ThickLineInstance> instances;
    bool empty()  const noexcept { return instances.empty(); }
    void clear()        noexcept { chunks.clear(); instances.clear(); }
};

/// All dashed lines sharing one @ref StyleKey, one @ref DashedLineInstance per
/// segment (each carries its @c phase_world). See @ref PrimitiveType::DashedLine.
struct DashedLineStyleBuffer : StyleBufferCommon {
    std::vector<DashedLineInstance> instances;
    bool empty()  const noexcept { return instances.empty(); }
    void clear()        noexcept { chunks.clear(); instances.clear(); }
};

/// All arrow heads sharing one @ref StyleKey, one @ref ArrowInstance per arrow
/// (GPU-instanced). See @ref PrimitiveType::Arrow.
struct ArrowStyleBuffer : StyleBufferCommon {
    std::vector<ArrowInstance> instances;
    bool empty()  const noexcept { return instances.empty(); }
    void clear()        noexcept { chunks.clear(); instances.clear(); }
};

/// One frame's worth of CPU-side geometry, grouped by primitive type and
/// keyed within each type by @ref StyleKey. Built by @ref rhi_renderer
/// at @c flush() time from the per-tile batches, uploaded to GPU buffers
/// by @ref RhiSceneRenderer::render(). Passed between threads by
/// @c shared_ptr<const SceneBuffers> so the render thread can keep
/// rendering an old scene while the main thread builds the next one.
struct SceneBuffers {
    std::unordered_map<StyleKey, ThinLineStyleBuffer>   thin_lines;
    std::unordered_map<StyleKey, FillRectStyleBuffer>   fill_rects;
    std::unordered_map<StyleKey, FillPolyStyleBuffer>   fill_polys;
    std::unordered_map<StyleKey, ThickLineStyleBuffer>  thick_lines;
    std::unordered_map<StyleKey, DashedLineStyleBuffer> dashed_lines;
    std::unordered_map<StyleKey, ArrowStyleBuffer>      arrows;

    /// True when no primitive type holds any style batch — i.e. nothing to
    /// upload or draw this frame.
    bool empty() const noexcept
    {
        return thin_lines.empty() && fill_rects.empty() && fill_polys.empty()
            && thick_lines.empty() && dashed_lines.empty() && arrows.empty();
    }

    /// Drop every style batch of every type, leaving an empty scene ready to
    /// be refilled by the next @ref rhi_renderer flush.
    void clear() noexcept
    {
        thin_lines.clear(); fill_rects.clear(); fill_polys.clear();
        thick_lines.clear(); dashed_lines.clear(); arrows.clear();
    }
};

} // namespace ezgl
