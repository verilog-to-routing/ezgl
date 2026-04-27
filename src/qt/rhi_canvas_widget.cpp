#include "ezgl/qt/rhi_canvas_widget.hpp"
#include "ezgl/logutils.hpp"

#include <rhi/qrhi.h>
#include <QOffscreenSurface>
#include <QResizeEvent>
#include <QShowEvent>
#include <QMutexLocker>

// Q_INIT_RESOURCE must be called at global scope (not inside a namespace).
// For static libraries, Qt resources are not automatically registered, so
// we force initialization once via a file-scope static.
static const int s_rhi_resources_init = []() {
#if defined(EZGL_RHI_GENERATED_SHADERS)
    Q_INIT_RESOURCE(ezgl_rhi_shaders);
#else
    Q_INIT_RESOURCE(shaders);
#endif
    return 0;
}();

namespace ezgl {

// ---- Construction / destruction --------------------------------------------

RhiCanvasWidget::RhiCanvasWidget(QWidget* parent)
    : QRhiWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setMirrorVertically(false);
}

RhiCanvasWidget::~RhiCanvasWidget() = default;

// ---- Thread-safe frame data API -------------------------------------------

void RhiCanvasWidget::set_frame_data(SceneBuffers       scene_buffers,
                                     const QMatrix4x4& world_to_ndc,
                                     const rectangle&  visible_world,
                                     const QImage&     overlay,
                                     QColor            bg_color)
{
    QMutexLocker lock(&m_frame_mutex);
    auto scene_ptr = std::make_shared<const SceneBuffers>(std::move(scene_buffers));
    m_pending_scene_buffers = scene_ptr;
    m_pending_mvp           = world_to_ndc;
    m_pending_visible_world = visible_world;
    m_pending_overlay       = overlay;
    m_pending_bg            = bg_color;
    m_frame_dirty           = true;
    m_mvp_dirty             = false;
    if (m_scene_renderer)
        m_scene_renderer->invalidate_geometry_cache();
}

void RhiCanvasWidget::set_mvp_only(const QMatrix4x4& world_to_ndc,
                                   const rectangle&  visible_world)
{
    QMutexLocker lock(&m_frame_mutex);
    m_pending_mvp           = world_to_ndc;
    m_pending_visible_world = visible_world;
    m_mvp_dirty             = true;
}

void RhiCanvasWidget::set_mvp_and_overlay(const QMatrix4x4& world_to_ndc,
                                          const rectangle&  visible_world,
                                          const QImage&     overlay)
{
    QMutexLocker lock(&m_frame_mutex);
    m_pending_mvp           = world_to_ndc;
    m_pending_visible_world = visible_world;
    m_pending_overlay       = overlay;
    m_mvp_dirty             = true;
}

// ---- QRhiWidget overrides --------------------------------------------------

void RhiCanvasWidget::initialize(QRhiCommandBuffer* /*cb*/)
{
    if (!m_scene_renderer)
        m_scene_renderer = std::make_unique<RhiSceneRenderer>();
    m_scene_renderer->initialize(rhi(), renderTarget()->renderPassDescriptor());
    q_info("RhiCanvasWidget: scene renderer initialized (%d frame slot(s)).",
           m_scene_renderer->frame_count());
}

void RhiCanvasWidget::render(QRhiCommandBuffer* cb)
{
    if (!m_scene_renderer || !m_scene_renderer->is_initialized())
        return;

    // Snapshot pending state under the mutex.
    std::shared_ptr<const SceneBuffers> scene;
    QMatrix4x4 mvp;
    rectangle   visible_world;
    QImage      overlay;
    QColor      bg;
    bool        geom_dirty;

    {
        QMutexLocker lock(&m_frame_mutex);
        if (!m_frame_dirty && !m_mvp_dirty)
            return;
        geom_dirty      = m_frame_dirty;
        scene           = m_pending_scene_buffers;
        mvp             = m_pending_mvp;
        visible_world   = m_pending_visible_world;
        overlay         = m_pending_overlay;
        bg              = m_pending_bg;
        m_frame_dirty   = false;
        m_mvp_dirty     = false;
        m_pending_scene_buffers.reset();
    }

    const int frame_slot = rhi()->currentFrameSlot();
    m_scene_renderer->render(cb, renderTarget(), renderTarget()->pixelSize(),
                              frame_slot, geom_dirty, scene,
                              mvp, visible_world, overlay, bg);
}

void RhiCanvasWidget::releaseResources()
{
    if (m_scene_renderer)
        m_scene_renderer->release();
}

void RhiCanvasWidget::resizeEvent(QResizeEvent* e)
{
    QRhiWidget::resizeEvent(e);
    if (width() > 0 && height() > 0)
        emit resized(width(), height());
}

void RhiCanvasWidget::showEvent(QShowEvent* e)
{
    QRhiWidget::showEvent(e);
    if (width() > 0 && height() > 0)
        emit resized(width(), height());
}

// ---- render_offscreen ------------------------------------------------------

QImage RhiCanvasWidget::render_offscreen(int               w,
                                         int               h,
                                         SceneBuffers      scene,
                                         const QMatrix4x4& mvp,
                                         const rectangle&  visible_world,
                                         const QImage&     overlay,
                                         QColor            bg)
{
    // Create a standalone QRhi (OpenGL + QOffscreenSurface) — no QRhiWidget,
    // no platform backing-store RHI required. Works on QT_QPA_PLATFORM=offscreen.
    QOffscreenSurface surface;
    surface.setFormat(QSurfaceFormat::defaultFormat());
    surface.create();
    if (!surface.isValid()) {
        qWarning("render_offscreen: QOffscreenSurface creation failed");
        return {};
    }

    QRhiGles2InitParams gl;
    gl.fallbackSurface = &surface;
    std::unique_ptr<QRhi> rhi(QRhi::create(QRhi::OpenGLES2, &gl));
    if (!rhi) {
        qWarning("render_offscreen: QRhi::create(OpenGLES2) failed — no OpenGL");
        return {};
    }

    // RGBA8 texture render target
    std::unique_ptr<QRhiTexture> color_tex(
        rhi->newTexture(QRhiTexture::RGBA8, QSize(w, h), 1,
                        QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource));
    if (!color_tex->create()) return {};

    std::unique_ptr<QRhiTextureRenderTarget> rt(
        rhi->newTextureRenderTarget(QRhiTextureRenderTargetDescription(color_tex.get())));
    std::unique_ptr<QRhiRenderPassDescriptor> rp(rt->newCompatibleRenderPassDescriptor());
    rt->setRenderPassDescriptor(rp.get());
    if (!rt->create()) return {};

    // Render using RhiSceneRenderer — the same pipeline code used by the widget
    RhiSceneRenderer renderer;
    renderer.initialize(rhi.get(), rp.get());

    QRhiCommandBuffer* cb = nullptr;
    if (rhi->beginOffscreenFrame(&cb) != QRhi::FrameOpSuccess) return {};

    auto scene_ptr = std::make_shared<const SceneBuffers>(std::move(scene));
    renderer.render(cb, rt.get(), QSize(w, h),
                    /*frame_slot=*/0, /*geom_dirty=*/true,
                    scene_ptr, mvp, visible_world, overlay, bg);

    // Pixel readback
    QRhiReadbackResult readback;
    QRhiResourceUpdateBatch* rb = rhi->nextResourceUpdateBatch();
    rb->readBackTexture({color_tex.get()}, &readback);
    cb->resourceUpdate(rb);

    rhi->endOffscreenFrame();

    if (readback.data.isEmpty()) return {};

    QImage result(reinterpret_cast<const uchar*>(readback.data.constData()),
                  w, h, QImage::Format_RGBA8888);
    return result.copy();
}

} // namespace ezgl
