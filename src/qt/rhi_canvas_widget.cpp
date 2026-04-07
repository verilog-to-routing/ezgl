#if defined(EZGL_QT) && defined(EZGL_RHI)

#include "ezgl/qt/rhi_canvas_widget.hpp"

#include <algorithm>
#include <rhi/qrhi.h>
#include <rhi/qshader.h>
#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QFile>
#include <QMutexLocker>
#include <chrono>
#include <limits>

#include "ezgl/qt/ezgl_qtcompat.hpp"

// Q_INIT_RESOURCE must be called at global scope (not inside a namespace).
// For static libraries, Qt resources are not automatically registered, so
// we force initialization once via a file-scope static.
static const int s_rhi_resources_init = []() {
    Q_INIT_RESOURCE(shaders);
    return 0;
}();

namespace {

// ---- file-scope helpers (not exposed in header) ----------------------------

constexpr std::size_t kMaxQrhiBufferBytes =
    std::size_t(std::numeric_limits<int>::max());
constexpr std::size_t kInitialVertexBufferBytes = 1 * 1024 * 1024;
constexpr std::size_t kInitialStyleBufferBytes  = 128 * 1024;
constexpr std::size_t kMaxVerticesPerChunk =
    std::min(kMaxQrhiBufferBytes / sizeof(ezgl::PosVertex),
             kMaxQrhiBufferBytes / sizeof(ezgl::StyleIndex));

QShader loadShader(const char* resource_path)
{
    QFile f(resource_path);
    if (!f.open(QIODevice::ReadOnly))
        qFatal("RhiCanvasWidget: cannot open shader resource %s", resource_path);
    return QShader::fromSerialized(f.readAll());
}

/**
 * Ensure buf is a dynamic QRhi buffer of at least needed_bytes.
 * Grows by doubling; the old buffer is scheduled for deferred deletion.
 */
bool ensureDynamicBuf(QRhi*                        rhi,
                      std::unique_ptr<QRhiBuffer>& buf,
                      QRhiBuffer::UsageFlags       usage,
                      std::size_t                  needed_bytes,
                      std::size_t                  initial_bytes)
{
    if (buf && std::size_t(buf->size()) >= needed_bytes)
        return false;

    if (needed_bytes > kMaxQrhiBufferBytes) {
        qFatal("RhiCanvasWidget: requested GPU buffer size %zu exceeds QRhi int-sized limit %zu",
               needed_bytes,
               kMaxQrhiBufferBytes);
    }

    std::size_t new_size = buf ? std::size_t(buf->size()) : initial_bytes;
    while (new_size < needed_bytes) {
        if (new_size > kMaxQrhiBufferBytes / 2) {
            new_size = kMaxQrhiBufferBytes;
            break;
        }
        new_size *= 2;
    }
    if (new_size < needed_bytes) {
        qFatal("RhiCanvasWidget: failed to grow GPU buffer to %zu bytes without overflow",
               needed_bytes);
    }

    QRhiBuffer* old = buf.release();
    if (old)
        old->deleteLater(); // deferred: safe while a frame referencing it is in flight

    buf.reset(rhi->newBuffer(QRhiBuffer::Dynamic, usage, int(new_size)));
    buf->create();
    return true;
}

void releaseDynamicBuf(std::unique_ptr<QRhiBuffer>& buf)
{
    QRhiBuffer* old = buf.release();
    if (old)
        old->deleteLater();
}

bool rectanglesIntersect(const ezgl::rectangle& a, const ezgl::rectangle& b)
{
    return !(a.right() < b.left()
          || a.left() > b.right()
          || a.top() < b.bottom()
          || a.bottom() > b.top());
}

struct ColorUniform {
    float rgba[4];
};
static_assert(sizeof(ColorUniform) == 16,
              "ColorUniform must match a std140 vec4");

struct PaletteUniformBlock {
    ColorUniform colors[ezgl::kMaxRhiStyleEntries];
};
static_assert(sizeof(PaletteUniformBlock) == ezgl::kMaxRhiStyleEntries * sizeof(ColorUniform),
              "PaletteUniformBlock must be tightly packed std140 vec4 entries");

ColorUniform makeColorUniform(std::uint32_t rgba)
{
    constexpr float kScale = 1.0f / 255.0f;
    return ColorUniform{{
        float((rgba >>  0) & 0xFF) * kScale,
        float((rgba >>  8) & 0xFF) * kScale,
        float((rgba >> 16) & 0xFF) * kScale,
        float((rgba >> 24) & 0xFF) * kScale
    }};
}

void buildPipeline(QRhi*                                    rhi,
                   std::unique_ptr<QRhiGraphicsPipeline>&   pso,
                   QRhiGraphicsPipeline::Topology            topology,
                   const QShader&                            vs,
                   const QShader&                            fs,
                   QRhiShaderResourceBindings*               srb,
                   QRhiRenderPassDescriptor*                  rpDesc)
{
    QRhiVertexInputLayout layout;
    layout.setBindings({
        QRhiVertexInputBinding(sizeof(ezgl::PosVertex)),
        QRhiVertexInputBinding(sizeof(ezgl::StyleIndex))
    });
    layout.setAttributes({
        QRhiVertexInputAttribute(0, 0, QRhiVertexInputAttribute::Float2,
                                 offsetof(ezgl::PosVertex, x)),
        QRhiVertexInputAttribute(1, 1, QRhiVertexInputAttribute::UNormByte, 0)
    });

    QRhiGraphicsPipeline::TargetBlend blend;
    blend.enable   = true;
    blend.srcColor = QRhiGraphicsPipeline::SrcAlpha;
    blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    blend.srcAlpha = QRhiGraphicsPipeline::One;
    blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;

    pso.reset(rhi->newGraphicsPipeline());
    pso->setTopology(topology);
    pso->setVertexInputLayout(layout);
    pso->setShaderStages({
        { QRhiShaderStage::Vertex,   vs },
        { QRhiShaderStage::Fragment, fs }
    });
    pso->setShaderResourceBindings(srb);
    pso->setRenderPassDescriptor(rpDesc);
    pso->setTargetBlends({ blend });
    pso->setDepthTest(false);
    pso->setDepthWrite(false);
    pso->create();
}

} // anonymous namespace

// ---- RhiCanvasWidget -------------------------------------------------------

namespace ezgl {

RhiCanvasWidget::RhiCanvasWidget(QWidget* parent)
    : QRhiWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    // mirrorVertically=false: our MVP (ortho with y-down origin) already
    // produces correct screen orientation without an extra vertical flip.
    setMirrorVertically(false);
}

// Destructor defined here so QRhiBuffer / QRhiGraphicsPipeline are complete.
RhiCanvasWidget::~RhiCanvasWidget() = default;

// ---- public API ------------------------------------------------------------

void RhiCanvasWidget::set_frame_data(std::vector<RhiTileBatch>  tiles,
                                     std::vector<std::uint32_t> palette_rgba,
                                     const QMatrix4x4&          world_to_ndc,
                                     const rectangle&           visible_world,
                                     const QImage&              overlay,
                                     QColor                     bg_color)
{
    QMutexLocker lock(&m_frame_mutex);
    m_pending_tiles        = std::move(tiles);
    m_pending_palette_rgba = std::move(palette_rgba);
    m_pending_mvp          = world_to_ndc;
    m_pending_visible_world = visible_world;
    m_pending_overlay      = overlay;
    m_pending_bg           = bg_color;
    m_frame_dirty          = true;
    m_mvp_dirty            = false;  // superseded by full frame
}

void RhiCanvasWidget::set_mvp_only(const QMatrix4x4& world_to_ndc,
                                   const rectangle&  visible_world)
{
    QMutexLocker lock(&m_frame_mutex);
    m_pending_mvp = world_to_ndc;
    m_pending_visible_world = visible_world;
    m_mvp_dirty   = true;
    // m_frame_dirty intentionally NOT set — vertex buffers are reused.
}

void RhiCanvasWidget::setResizeCallback(std::function<void(int,int)> cb)
{
    m_resize_cb = std::move(cb);
}

void RhiCanvasWidget::setPreResizeCallback(std::function<void()> cb)
{
    m_pre_resize_cb = std::move(cb);
}

// ---- QRhiWidget overrides --------------------------------------------------

void RhiCanvasWidget::initialize(QRhiCommandBuffer* /*cb*/)
{
    QShader vs = loadShader(":/ezgl/line.vert.qsb");
    QShader fs = loadShader(":/ezgl/line.frag.qsb");

    // Uniform buffers: one MVP block plus a small shared palette.
    m_mvp_ubuf.reset(rhi()->newBuffer(QRhiBuffer::Dynamic,
                                      QRhiBuffer::UniformBuffer,
                                      64));
    m_mvp_ubuf->create();
    m_palette_ubuf.reset(rhi()->newBuffer(QRhiBuffer::Dynamic,
                                          QRhiBuffer::UniformBuffer,
                                          sizeof(PaletteUniformBlock)));
    m_palette_ubuf->create();

    // Shader resource bindings: MVP at binding 0, palette at binding 1.
    m_srb.reset(rhi()->newShaderResourceBindings());
    m_srb->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(
            0,
            QRhiShaderResourceBinding::VertexStage,
            m_mvp_ubuf.get()),
        QRhiShaderResourceBinding::uniformBuffer(
            1,
            QRhiShaderResourceBinding::FragmentStage,
            m_palette_ubuf.get())
    });
    m_srb->create();

    auto* rpDesc = renderTarget()->renderPassDescriptor();
    buildPipeline(rhi(), m_line_pso, QRhiGraphicsPipeline::Lines,     vs, fs, m_srb.get(), rpDesc);
    buildPipeline(rhi(), m_fill_pso, QRhiGraphicsPipeline::Triangles, vs, fs, m_srb.get(), rpDesc);
    buildPipeline(rhi(), m_draw_pso, QRhiGraphicsPipeline::Lines,     vs, fs, m_srb.get(), rpDesc);

    m_initialized = true;
}

void RhiCanvasWidget::render(QRhiCommandBuffer* cb)
{
    if (!m_initialized)
        return;

    const auto frame_start = std::chrono::steady_clock::now();

    // --- Snapshot pending frame under lock -----------------------------------
    std::vector<RhiTileBatch> tiles;
    std::vector<std::uint32_t> palette_rgba;
    QMatrix4x4 mvp;
    rectangle  visible_world;
    QColor     bg;
    bool       geom_dirty;

    {
        QMutexLocker lock(&m_frame_mutex);
        if (!m_frame_dirty && !m_mvp_dirty)
            return;

        geom_dirty = m_frame_dirty;
        mvp        = m_pending_mvp;
        visible_world = m_pending_visible_world;
        bg         = m_pending_bg;

        if (geom_dirty) {
            tiles        = std::move(m_pending_tiles);
            palette_rgba = std::move(m_pending_palette_rgba);
        }
        m_frame_dirty = false;
        m_mvp_dirty   = false;
        // m_pending_overlay stays; paintEvent() reads it via the mutex.
    }

    // --- Upload to GPU -------------------------------------------------------
    QRhiResourceUpdateBatch* u = rhi()->nextResourceUpdateBatch();

    // Always update MVP (column-major, as OpenGL/SPIR-V expect).
    u->updateDynamicBuffer(m_mvp_ubuf.get(), 0, 64, mvp.constData());

    if (geom_dirty) {
        if (palette_rgba.size() > kMaxRhiStyleEntries) {
            qFatal("RhiCanvasWidget: palette size %zu exceeds limit %zu",
                   palette_rgba.size(), kMaxRhiStyleEntries);
        }

        auto releaseChunks = [](std::vector<StreamChunk>& chunks) {
            for (StreamChunk& chunk : chunks) {
                releaseDynamicBuf(chunk.pos_vbuf);
                releaseDynamicBuf(chunk.style_vbuf);
                chunk.count = 0;
            }
            chunks.clear();
        };
        auto trimChunks = [&](std::vector<StreamChunk>& chunks, std::size_t keep_count) {
            for (std::size_t i = keep_count; i < chunks.size(); ++i) {
                releaseDynamicBuf(chunks[i].pos_vbuf);
                releaseDynamicBuf(chunks[i].style_vbuf);
                chunks[i].count = 0;
            }
            chunks.resize(keep_count);
        };
        auto uploadStream = [&](std::vector<StreamChunk>& chunks,
                                const auto&               verts,
                                const auto&               styles) {
            if (styles.size() != verts.size()) {
                qFatal("RhiCanvasWidget: style-stream size mismatch with vertex stream");
            }

            const std::size_t vertex_count = verts.size();
            const std::size_t chunk_count =
                vertex_count == 0 ? 0 : (vertex_count + kMaxVerticesPerChunk - 1) / kMaxVerticesPerChunk;

            if (chunks.size() < chunk_count)
                chunks.resize(chunk_count);

            for (std::size_t chunk_idx = 0; chunk_idx < chunk_count; ++chunk_idx) {
                const std::size_t begin = chunk_idx * kMaxVerticesPerChunk;
                const std::size_t count = std::min(kMaxVerticesPerChunk, vertex_count - begin);
                StreamChunk& chunk = chunks[chunk_idx];
                const std::size_t pos_bytes = count * sizeof(verts[0]);
                const std::size_t style_bytes = count * sizeof(styles[0]);

                ensureDynamicBuf(rhi(),
                                 chunk.pos_vbuf,
                                 QRhiBuffer::VertexBuffer,
                                 pos_bytes,
                                 kInitialVertexBufferBytes);
                ensureDynamicBuf(rhi(),
                                 chunk.style_vbuf,
                                 QRhiBuffer::VertexBuffer,
                                 style_bytes,
                                 kInitialStyleBufferBytes);
                u->updateDynamicBuffer(chunk.pos_vbuf.get(),
                                       0,
                                       int(pos_bytes),
                                       verts.data() + begin);
                u->updateDynamicBuffer(chunk.style_vbuf.get(),
                                       0,
                                       int(style_bytes),
                                       styles.data() + begin);
                chunk.count = quint32(count);
            }

            trimChunks(chunks, chunk_count);
        };
        auto releaseTile = [&](GpuTileBatch& tile) {
            releaseChunks(tile.line_chunks);
            releaseChunks(tile.fill_chunks);
            releaseChunks(tile.draw_chunks);
        };

        if (m_gpu_tiles.size() > tiles.size()) {
            for (std::size_t i = tiles.size(); i < m_gpu_tiles.size(); ++i)
                releaseTile(m_gpu_tiles[i]);
            m_gpu_tiles.resize(tiles.size());
        } else if (m_gpu_tiles.size() < tiles.size()) {
            m_gpu_tiles.resize(tiles.size());
        }

        for (std::size_t i = 0; i < tiles.size(); ++i) {
            const RhiTileBatch& tile = tiles[i];
            GpuTileBatch& gpu_tile = m_gpu_tiles[i];
            gpu_tile.world_bounds = tile.world_bounds;
            uploadStream(gpu_tile.line_chunks, tile.line_verts, tile.line_styles);
            uploadStream(gpu_tile.fill_chunks, tile.fill_verts, tile.fill_styles);
            uploadStream(gpu_tile.draw_chunks, tile.draw_verts, tile.draw_styles);
        }

        PaletteUniformBlock palette_data{};
        const std::size_t palette_count =
            std::min<std::size_t>(palette_rgba.size(), kMaxRhiStyleEntries);
        for (std::size_t i = 0; i < palette_count; ++i)
            palette_data.colors[i] = makeColorUniform(palette_rgba[i]);
        u->updateDynamicBuffer(m_palette_ubuf.get(),
                               0,
                               sizeof(PaletteUniformBlock),
                               &palette_data);
    }
    // Camera-only frame: tiled vertex/style buffers and palette are reused.

    // --- Record draw commands ------------------------------------------------
    cb->beginPass(renderTarget(), bg, { 1.0f, 0 }, u);
    cb->setViewport(QRhiViewport(0, 0, float(width()), float(height())));

    auto drawChunks = [&](std::unique_ptr<QRhiGraphicsPipeline>& pso,
                          const std::vector<StreamChunk>&        chunks) {
        for (const StreamChunk& chunk : chunks) {
            if (chunk.count == 0)
                continue;

            cb->setGraphicsPipeline(pso.get());
            cb->setShaderResources(m_srb.get());
            const QRhiCommandBuffer::VertexInput inputs[] = {
                { chunk.pos_vbuf.get(), 0 },
                { chunk.style_vbuf.get(), 0 }
            };
            cb->setVertexInput(0, 2, inputs);
            cb->draw(chunk.count);
        }
    };

    std::size_t visible_tile_count = 0;
    for (const GpuTileBatch& tile : m_gpu_tiles) {
        if (!rectanglesIntersect(tile.world_bounds, visible_world))
            continue;

        ++visible_tile_count;
        drawChunks(m_line_pso, tile.line_chunks);
        drawChunks(m_fill_pso, tile.fill_chunks);
        drawChunks(m_draw_pso, tile.draw_chunks);
    }

    cb->endPass();

    const auto frame_end = std::chrono::steady_clock::now();
    const double frame_ms = std::chrono::duration<double, std::milli>(frame_end - frame_start).count();
    auto countChunkVertices = [](const std::vector<StreamChunk>& chunks) {
        unsigned long long total = 0;
        for (const StreamChunk& chunk : chunks)
            total += chunk.count;
        return total;
    };
    unsigned long long line_verts = 0;
    unsigned long long fill_verts = 0;
    unsigned long long draw_verts = 0;
    for (const GpuTileBatch& tile : m_gpu_tiles) {
        line_verts += countChunkVertices(tile.line_chunks);
        fill_verts += countChunkVertices(tile.fill_chunks);
        draw_verts += countChunkVertices(tile.draw_chunks);
    }
    g_debug("RHI render() CPU time %.3f ms (geom_dirty=%d, tiles=%zu, visible_tiles=%zu, line_verts=%llu, fill_verts=%llu, draw_verts=%llu)",
            frame_ms,
            int(geom_dirty),
            m_gpu_tiles.size(),
            visible_tile_count,
            line_verts,
            fill_verts,
            draw_verts);
}

void RhiCanvasWidget::releaseResources()
{
    auto releaseChunks = [](std::vector<StreamChunk>& chunks) {
        for (StreamChunk& chunk : chunks) {
            releaseDynamicBuf(chunk.pos_vbuf);
            releaseDynamicBuf(chunk.style_vbuf);
            chunk.count = 0;
        }
        chunks.clear();
    };

    m_draw_pso.reset();
    m_fill_pso.reset();
    m_line_pso.reset();
    m_srb.reset();
    for (GpuTileBatch& tile : m_gpu_tiles) {
        releaseChunks(tile.line_chunks);
        releaseChunks(tile.fill_chunks);
        releaseChunks(tile.draw_chunks);
    }
    m_gpu_tiles.clear();
    m_palette_ubuf.reset();
    m_mvp_ubuf.reset();
    m_initialized = false;
}

void RhiCanvasWidget::paintEvent(QPaintEvent* e)
{
    // QRhiWidget::paintEvent renders the GPU texture to the widget surface.
    QRhiWidget::paintEvent(e);

    // Composite QPainter overlay (text, arcs, polygons) on top.
    QImage overlay;
    {
        QMutexLocker lock(&m_frame_mutex);
        overlay = m_pending_overlay; // shallow COW copy
    }
    if (!overlay.isNull()) {
        QPainter p(this);
        p.drawImage(rect(), overlay, overlay.rect());
    }
}

void RhiCanvasWidget::resizeEvent(QResizeEvent* e)
{
    if (m_pre_resize_cb)
        m_pre_resize_cb();
    QRhiWidget::resizeEvent(e);
    if (width() > 0 && height() > 0 && m_resize_cb)
        m_resize_cb(width(), height());
}

} // namespace ezgl

#endif // EZGL_QT && EZGL_RHI
