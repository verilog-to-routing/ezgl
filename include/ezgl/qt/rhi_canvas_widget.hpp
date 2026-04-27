#pragma once

#include "ezgl/qt/rhi_types.hpp"
#include "ezgl/qt/rhi_scene_renderer.hpp"

#include <QRhiWidget>
#include <QImage>
#include <QMatrix4x4>
#include <QMutex>
#include <QColor>
#include <memory>

namespace ezgl {

/**
 * QWidget subclass (via QRhiWidget) that renders line and rect primitives on
 * the GPU (Vulkan / Metal / D3D12 / OpenGL via Qt RHI).
 *
 * Responsibilities:
 *  - Thread-safe receive of frame data from rhi_renderer (set_frame_data etc.)
 *  - Delegate all GPU pipeline / rendering work to RhiSceneRenderer
 *  - Provide render_offscreen() for headless save-graphics paths
 */
class RhiCanvasWidget : public QRhiWidget {
    Q_OBJECT
public:
    explicit RhiCanvasWidget(QWidget* parent = nullptr);
    ~RhiCanvasWidget() override;

    // ---- Frame data API (thread-safe, called from rhi_renderer) -------------

    void set_frame_data(SceneBuffers      scene_buffers,
                        const QMatrix4x4& world_to_ndc,
                        const rectangle&  visible_world,
                        const QImage&     overlay,
                        QColor            bg_color);

    void set_mvp_only(const QMatrix4x4& world_to_ndc,
                      const rectangle&  visible_world);

    void set_mvp_and_overlay(const QMatrix4x4& world_to_ndc,
                             const rectangle&  visible_world,
                             const QImage&     overlay);

    // ---- Headless rendering (no QRhiWidget::grab(), works on offscreen QPA) -

    static QImage render_offscreen(int               w,
                                   int               h,
                                   SceneBuffers      scene,
                                   const QMatrix4x4& mvp,
                                   const rectangle&  visible_world,
                                   const QImage&     overlay,
                                   QColor            bg);

signals:
    void resized(int w, int h);

protected:
    void initialize(QRhiCommandBuffer* cb) override;
    void render(QRhiCommandBuffer* cb) override;
    void releaseResources() override;
    void resizeEvent(QResizeEvent* e) override;
    void showEvent(QShowEvent* e) override;

private:
    // ---- GPU rendering (all pipeline/frame-resource state lives here) -------
    std::unique_ptr<RhiSceneRenderer> m_scene_renderer;

    // ---- Pending frame state (written by rhi_renderer, read by render()) ----
    mutable QMutex                       m_frame_mutex;
    std::shared_ptr<const SceneBuffers>  m_pending_scene_buffers;
    QMatrix4x4                           m_pending_mvp;
    rectangle                            m_pending_visible_world;
    QImage                               m_pending_overlay;
    QColor                               m_pending_bg  { Qt::white };
    bool                                 m_frame_dirty = false;
    bool                                 m_mvp_dirty   = false;
};

/**
 * Returns true if a GPU-accelerated QRhi backend can be created on this
 * machine (Metal on macOS, D3D11 on Windows, OpenGL on Linux/other). The
 * result is cached after the first call so the probe is only performed once
 * per process. Returns false on headless CI without a GPU or GPU drivers.
 */
bool probe_rhi();

} // namespace ezgl
