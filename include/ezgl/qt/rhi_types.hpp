#pragma once

#include "ezgl/rectangle.hpp"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace ezgl {

// ---- Vertex / instance layouts ---------------------------------------------

struct PosVertex {
    float x, y;
};
static_assert(sizeof(PosVertex) == 8, "PosVertex must be 8 bytes");

struct QuadCorner {
    float t;
    float side;
};
static_assert(sizeof(QuadCorner) == 8, "QuadCorner must be 8 bytes");

struct ThickLineInstance {
    float x0, y0;
    float x1, y1;
};
static_assert(sizeof(ThickLineInstance) == 16, "ThickLineInstance must be 16 bytes");

struct DashedLineInstance {
    float x0, y0;
    float x1, y1;
    float phase_world;
};
static_assert(sizeof(DashedLineInstance) == 20, "DashedLineInstance must be 20 bytes");

struct FillRectInstance {
    float x0, y0;
    float x1, y1;
};
static_assert(sizeof(FillRectInstance) == 16, "FillRectInstance must be 16 bytes");

// One arrow head per instance: world anchor + world direction. Fixed-pixel
// expansion to a 3-vertex triangle happens in the vertex shader so the on-
// screen size never grows on zoom-in. Direction can be any nonzero length —
// the shader normalises before computing the screen-space basis.
struct ArrowInstance {
    float ax, ay;
    float dx, dy;
};
static_assert(sizeof(ArrowInstance) == 16, "ArrowInstance must be 16 bytes");

// ---- Style key encoding ----------------------------------------------------

using StyleKey = std::uint64_t;

enum class PrimitiveType : std::uint8_t {
    ThinLine,
    FilledRect,
    FilledPoly,
    ThickLine,
    DashedLine,
    Arrow,            // GPU-instanced arrow head; line_width field reused as arrow_size_px
};

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

struct Chunk {
    rectangle     world_bounds;
    std::uint32_t offset = 0;
    std::uint32_t count  = 0;
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
