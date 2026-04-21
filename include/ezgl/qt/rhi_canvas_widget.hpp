#pragma once

#include "ezgl/rectangle.hpp"

#include <QRhiWidget>
#include <QImage>
#include <QMatrix4x4>
#include <QMutex>
#include <QColor>
#include <cstdint>
#include <unordered_map>
#include <memory>
#include <vector>
#include <functional>

// Forward-declare private Qt RHI types so we can hold them as unique_ptr members
// without pulling private headers into every translation unit.
QT_FORWARD_DECLARE_CLASS(QRhiBuffer)
QT_FORWARD_DECLARE_CLASS(QRhiShaderResourceBindings)
QT_FORWARD_DECLARE_CLASS(QRhiGraphicsPipeline)
QT_FORWARD_DECLARE_CLASS(QRhiSampler)
QT_FORWARD_DECLARE_CLASS(QRhiTexture)

namespace ezgl {

/**
 * Packed GPU vertex: world-space position only.
 * Layout matches the QRhiVertexInputAttribute setup in RhiCanvasWidget.
 *   Offset 0: float x, y   (8 bytes)
 */
struct PosVertex {
    float x, y;
};
static_assert(sizeof(PosVertex) == 8, "PosVertex must be 8 bytes");

/**
 * One corner of the thick-line quad template (4 total, constant buffer).
 *
 *   t    : 0.0 = at line start,  1.0 = at line end
 *   side : -1.0 = left edge,    +1.0 = right edge
 *
 * The same 4-corner buffer is reused for every thick-line draw call via
 * instanced rendering, so it never grows with the number of lines.
 */
struct QuadCorner {
    float t;
    float side;
};
static_assert(sizeof(QuadCorner) == 8, "QuadCorner must be 8 bytes");

/**
 * Per-instance data for one thick-line segment (instanced rendering).
 *
 * Width is part of the per-style uniform, so all segments in the same style
 * batch share it without repeating it in every instance.
 *
 *   Offset  0 : float x0, y0   — world-space start
 *   Offset  8 : float x1, y1   — world-space end
 */
struct ThickLineInstance {
    float x0, y0;
    float x1, y1;
};
static_assert(sizeof(ThickLineInstance) == 16, "ThickLineInstance must be 16 bytes");

/**
 * Per-instance data for one dashed-line segment (instanced rendering).
 *
 * Width, dash, and gap are per-style uniforms. The only dashed-only instance
 * value is phase_world, which preserves dash phase across tile-clipped
 * segments.
 *
 *   Offset  0 : float x0, y0         — world-space clipped start
 *   Offset  8 : float x1, y1         — world-space clipped end
 *   Offset 16 : float phase_world    — world-space distance from original segment start to x0/y0
 */
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

using StyleKey = std::uint64_t;

enum class PrimitiveType : std::uint8_t {
    ThinLine,
    FilledRect,
    FilledPoly,
    ThickLine,
    DashedLine,
};

inline constexpr StyleKey pack_style_key(PrimitiveType primitive_type,
                                         std::uint32_t rgba,
                                         std::uint16_t line_width_px,
                                         std::uint8_t line_dash) noexcept
{
    return StyleKey(rgba)
        | (StyleKey(line_width_px) << 32)
        | (StyleKey(line_dash) << 48)
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

struct Chunk {
    rectangle     world_bounds;
    std::uint32_t offset = 0;
    std::uint32_t count = 0;
};

struct StyleBufferCommon {
    StyleKey               style_key = 0;
    std::uint32_t          rgba = 0;
    std::vector<Chunk>     chunks;
};

struct ThinLineStyleBuffer : StyleBufferCommon {
    std::vector<PosVertex> verts;

    bool empty() const noexcept { return verts.empty(); }
    void clear() noexcept
    {
        chunks.clear();
        verts.clear();
    }
};

struct FillRectStyleBuffer : StyleBufferCommon {
    std::vector<FillRectInstance> instances;

    bool empty() const noexcept { return instances.empty(); }
    void clear() noexcept
    {
        chunks.clear();
        instances.clear();
    }
};

struct FillPolyStyleBuffer : StyleBufferCommon {
    std::vector<PosVertex> verts;

    bool empty() const noexcept { return verts.empty(); }
    void clear() noexcept
    {
        chunks.clear();
        verts.clear();
    }
};

struct ThickLineStyleBuffer : StyleBufferCommon {
    std::vector<ThickLineInstance> instances;

    bool empty() const noexcept { return instances.empty(); }
    void clear() noexcept
    {
        chunks.clear();
        instances.clear();
    }
};

struct DashedLineStyleBuffer : StyleBufferCommon {
    std::vector<DashedLineInstance> instances;

    bool empty() const noexcept { return instances.empty(); }
    void clear() noexcept
    {
        chunks.clear();
        instances.clear();
    }
};

struct SceneBuffers {
    std::unordered_map<StyleKey, ThinLineStyleBuffer>   thin_lines;
    std::unordered_map<StyleKey, FillRectStyleBuffer>   fill_rects;
    std::unordered_map<StyleKey, FillPolyStyleBuffer>   fill_polys;
    std::unordered_map<StyleKey, ThickLineStyleBuffer>  thick_lines;
    std::unordered_map<StyleKey, DashedLineStyleBuffer> dashed_lines;

    bool empty() const noexcept
    {
        return thin_lines.empty()
            && fill_rects.empty()
            && fill_polys.empty()
            && thick_lines.empty()
            && dashed_lines.empty();
    }

    void clear() noexcept
    {
        thin_lines.clear();
        fill_rects.clear();
        fill_polys.clear();
        thick_lines.clear();
        dashed_lines.clear();
    }
};

/**
 * QWidget subclass (via QRhiWidget) that renders line and rect primitives on
 * the GPU (Vulkan / Metal / D3D12 / OpenGL via Qt RHI). The text/arc overlay
 * is uploaded as a texture and blended in a final full-screen quad pass inside
 * render(), avoiding QWidget-side compositing issues with QRhiWidget.
 *
 * Ownership model:
 *   - canvas::initialize() creates this widget in place of DrawingAreaWidget.
 *   - rhi_renderer calls set_frame_data() + update() each frame.
 *   - render() runs on Qt's render/GUI thread.
 */
class RhiCanvasWidget : public QRhiWidget {
    Q_OBJECT
public:
    explicit RhiCanvasWidget(QWidget* parent = nullptr);
    // Destructor defined in .cpp (QRhiBuffer etc. must be complete there).
    ~RhiCanvasWidget() override;

    /**
     * Push a full frame (geometry + transform).  Thread-safe.
     *
     * Vertex coordinates are in world space; the GPU applies world_to_ndc to
     * transform them. Call this when scene geometry has changed.
     *
     * @param scene_buffers   Scene-wide style buffers with chunk bounds for culling.
     * @param world_to_ndc    Matrix mapping world coords → NDC.
     * @param visible_world   Current visible world bounds used for tile selection.
     * @param overlay         Transparent QImage with text / arcs drawn by QPainter.
     * @param bg_color        Clear color for the render target.
     */
    void set_frame_data(SceneBuffers             scene_buffers,
                        const QMatrix4x4&       world_to_ndc,
                        const rectangle&        visible_world,
                        const QImage&           overlay,
                        QColor                  bg_color);

    /**
     * Update only the camera transform (no geometry re-upload).  Thread-safe.
     *
     * Call this when the camera has panned or zoomed but no primitives changed.
     * The widget re-renders with the existing vertex buffers and the new MVP.
     */
    void set_mvp_only(const QMatrix4x4& world_to_ndc,
                      const rectangle&  visible_world);

    /**
     * Update the camera transform and replace the overlay image without
     * re-uploading geometry. Thread-safe.
     */
    void set_mvp_and_overlay(const QMatrix4x4& world_to_ndc,
                             const rectangle&  visible_world,
                             const QImage&     overlay);

signals:
    void resized(int w, int h);

protected:
    // QRhiWidget interface
    void initialize(QRhiCommandBuffer* cb) override;
    void render(QRhiCommandBuffer* cb) override;
    void releaseResources() override;

    void resizeEvent(QResizeEvent* e) override;
    void showEvent(QShowEvent* e) override;

private:
    struct GpuChunk {
        rectangle world_bounds;
        quint32 buffer_index = 0;
        quint32 byte_offset = 0;
        quint32 count = 0;
    };

    struct GpuStyleBuffer {
        StyleKey               style_key = 0;
        std::uint32_t          rgba = 0;
        quint32                style_offset = 0;
        std::vector<GpuChunk>  chunks;
    };

    struct GpuSceneBuffers {
        std::vector<GpuStyleBuffer> thin_lines;
        std::vector<GpuStyleBuffer> fill_rects;
        std::vector<GpuStyleBuffer> fill_polys;
        std::vector<GpuStyleBuffer> thick_lines;
        std::vector<GpuStyleBuffer> dashed_lines;

        void clear()
        {
            thin_lines.clear();
            fill_rects.clear();
            fill_polys.clear();
            thick_lines.clear();
            dashed_lines.clear();
        }
    };

    struct FrameResources {
        std::unique_ptr<QRhiBuffer>              mvp_ubuf;
        std::unique_ptr<QRhiBuffer>              style_ubuf;
        std::vector<std::unique_ptr<QRhiBuffer>> thin_line_vbufs;
        std::vector<std::unique_ptr<QRhiBuffer>> fill_rect_instance_vbufs;
        std::vector<std::unique_ptr<QRhiBuffer>> fill_poly_vbufs;
        std::vector<std::unique_ptr<QRhiBuffer>> thick_line_instance_vbufs;
        std::vector<std::unique_ptr<QRhiBuffer>> dashed_line_instance_vbufs;
        std::unique_ptr<QRhiTexture>             overlay_tex;
        std::unique_ptr<QRhiShaderResourceBindings> overlay_srb;
        std::unique_ptr<QRhiShaderResourceBindings> srb;
        GpuSceneBuffers                          gpu_scene;
    };

    // GPU resources — unique_ptr keeps them alive between initialize/release.
    // Mutable per-frame resources are duplicated across QRhi frame slots so
    // camera-only updates do not rewrite data that may still be in flight.
    std::vector<FrameResources>                m_frame_resources;
    // Thick-line instanced rendering:
    //   m_thick_line_corner_vbuf  — 4 QuadCorner values, immutable, shared by all draws.
    std::unique_ptr<QRhiBuffer>                m_thick_line_corner_vbuf;
    std::unique_ptr<QRhiBuffer>                m_overlay_quad_vbuf;
    std::unique_ptr<QRhiSampler>               m_overlay_sampler;
    std::unique_ptr<QRhiGraphicsPipeline>       m_line_pso;
    std::unique_ptr<QRhiGraphicsPipeline>       m_fill_rect_pso;
    std::unique_ptr<QRhiGraphicsPipeline>       m_fill_poly_pso;
    std::unique_ptr<QRhiGraphicsPipeline>       m_thick_line_pso;
    std::unique_ptr<QRhiGraphicsPipeline>       m_dashed_line_pso;
    std::unique_ptr<QRhiGraphicsPipeline>       m_overlay_pso;
    bool m_initialized = false;

    // Pending frame state (written by set_frame_data / set_mvp_only,
    // consumed by render()).
    mutable QMutex                                     m_frame_mutex;
    std::shared_ptr<const SceneBuffers>                m_pending_scene_buffers;
    QMatrix4x4                                         m_pending_mvp;
    rectangle                                          m_pending_visible_world;
    QImage                                             m_pending_overlay;
    QColor                                             m_pending_bg  { Qt::white };
    bool                                               m_frame_dirty = false;  // geometry + MVP changed
    bool                                               m_mvp_dirty   = false;  // only MVP/overlay changed

    // CPU-side cache of the latest full frame. Used to lazily repopulate
    // QRhi frame slots after resize/re-init or when a slot has not yet
    // received the current geometry revision.
    std::shared_ptr<const SceneBuffers>                m_cached_scene_buffers;
    std::vector<bool>                                  m_frame_slot_geom_valid;

    // Canvas hooks
};

} // namespace ezgl
