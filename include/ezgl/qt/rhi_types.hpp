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

enum class PrimitiveType : std::uint8_t {
    ThinLine,
    FilledRect,
    FilledPoly,
    ThickLine,
    DashedLine,
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

inline constexpr std::uint16_t style_key_line_width(StyleKey key) noexcept
{
    return std::uint16_t((key >> 32) & 0xFFFFu);
}

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

struct StyleBufferCommon {
    StyleKey           style_key = 0;
    std::uint32_t      rgba      = 0;
    std::vector<Chunk> chunks;
};

struct ThinLineStyleBuffer : StyleBufferCommon {
    std::vector<PosVertex> verts;
    bool empty()  const noexcept { return verts.empty(); }
    void clear()        noexcept { chunks.clear(); verts.clear(); }
};

struct FillRectStyleBuffer : StyleBufferCommon {
    std::vector<FillRectInstance> instances;
    bool empty()  const noexcept { return instances.empty(); }
    void clear()        noexcept { chunks.clear(); instances.clear(); }
};

struct FillPolyStyleBuffer : StyleBufferCommon {
    std::vector<PosVertex> verts;
    bool empty()  const noexcept { return verts.empty(); }
    void clear()        noexcept { chunks.clear(); verts.clear(); }
};

struct ThickLineStyleBuffer : StyleBufferCommon {
    std::vector<ThickLineInstance> instances;
    bool empty()  const noexcept { return instances.empty(); }
    void clear()        noexcept { chunks.clear(); instances.clear(); }
};

struct DashedLineStyleBuffer : StyleBufferCommon {
    std::vector<DashedLineInstance> instances;
    bool empty()  const noexcept { return instances.empty(); }
    void clear()        noexcept { chunks.clear(); instances.clear(); }
};

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

    bool empty() const noexcept
    {
        return thin_lines.empty() && fill_rects.empty() && fill_polys.empty()
            && thick_lines.empty() && dashed_lines.empty() && arrows.empty();
    }

    void clear() noexcept
    {
        thin_lines.clear(); fill_rects.clear(); fill_polys.clear();
        thick_lines.clear(); dashed_lines.clear(); arrows.clear();
    }
};

} // namespace ezgl
