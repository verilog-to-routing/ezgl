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
 * @brief @c QRhiWidget subclass that displays ezgl scenes via the GPU
 * (Vulkan / Metal / D3D12 / OpenGL via Qt RHI).
 *
 * Acts as the bridge between @ref rhi_renderer (main thread, records
 * primitives) and @ref RhiSceneRenderer (render thread, owns all GPU
 * resources). Constructs with @c setSampleCount(EZGL_RHI_SAMPLE_COUNT) so
 * Qt allocates an MSAA color buffer plus a single-sample resolve buffer
 * for the swapchain.
 *
 * @par Thread-safe frame inbox
 * @ref rhi_renderer calls @ref set_frame_data / @ref set_mvp_only /
 * @ref set_mvp_and_overlay on the main thread; @ref render() consumes the
 * pending state on the Qt render thread. The inbox fields
 * (@c m_pending_scene_buffers, @c m_pending_mvp, @c m_pending_overlay,
 * @c m_pending_bg) are guarded by @c m_frame_mutex. Writers hold it
 * briefly to swap state and set @c m_frame_dirty / @c m_mvp_dirty. The
 * render thread snapshots and clears under the same lock at the top of
 * @ref render(). The scene is held as @c shared_ptr<const SceneBuffers>
 * so the render thread can keep using the previous frame while the main
 * thread builds the next one without copying.
 *
 * @par Responsibilities
 *  - Thread-safe receipt of frame data from @ref rhi_renderer.
 *  - Delegate all GPU pipeline / draw work to @ref RhiSceneRenderer
 *    (owned, created lazily in @ref initialize()).
 *  - Provide @ref render_offscreen() for headless `save_graphics`
 *    paths that need a PNG without a live QRhiWidget.
 *
 * @par Offscreen QPA caveat
 * @c QRhiWidget cannot acquire a QRhi under @c QT_QPA_PLATFORM=offscreen.
 * Callers that need that combination should detect it before
 * instantiating @ref RhiCanvasWidget and fall back to a non-rhi
 * backend.
 */
class RhiCanvasWidget : public QRhiWidget {
    Q_OBJECT
public:
    explicit RhiCanvasWidget(QWidget* parent = nullptr);
    ~RhiCanvasWidget() override;

    // ---- Frame data API (thread-safe, called from rhi_renderer) -------------

    /// Full frame update: replace scene geometry, MVP, overlay, and
    /// background. Marks both geometry and MVP dirty. Called after a
    /// full @ref rhi_renderer::flush().
    void set_frame_data(SceneBuffers      scene_buffers,
                        const QMatrix4x4& world_to_ndc,
                        const rectangle&  visible_world,
                        const QImage&     overlay,
                        QColor            bg_color);

    /// MVP-only update for pan/zoom with no scene/overlay change. Marks
    /// MVP dirty without invalidating geometry. The render thread will
    /// re-render the cached scene with the new transform.
    void set_mvp_only(const QMatrix4x4& world_to_ndc,
                      const rectangle&  visible_world);

    /// MVP + overlay update: scene geometry unchanged, but overlay text /
    /// arcs were re-laid out for the new camera. Used by
    /// @ref rhi_renderer::flush_mvp_only().
    void set_mvp_and_overlay(const QMatrix4x4& world_to_ndc,
                             const rectangle&  visible_world,
                             const QImage&     overlay);

    // ---- Headless rendering (no QRhiWidget::grab(), works on offscreen QPA) -

    /**
     * Headless render — @c static utility, does NOT use any
     * @ref RhiCanvasWidget instance. It only lives here because it
     * shares the @ref RhiSceneRenderer setup logic with the on-screen
     * path.
     *
     * Builds a standalone @c QRhi via @c create_headless_rhi (tries
     * D3D11 on Windows / Metal on macOS / OpenGL 4.1 core elsewhere),
     * runs @ref RhiSceneRenderer against an offscreen MSAA render
     * target, then reads pixels back as a QImage.
     *
     * The render target is a 4x-MSAA RGBA8 color texture with an
     * attached single-sample resolve texture. QRhi resolves MSAA into
     * the resolve texture at render-pass end; @c readBackTexture is then
     * issued against the resolve texture (MSAA textures aren't directly
     * readable). The result is Y-flipped if the chosen backend reports
     * @c isYUpInFramebuffer() (OpenGL only) so the returned QImage
     * matches Qt's top-down convention.
     *
     * Used by @c rhi_backend::render_to_image() for @c save_graphics and
     * the headless visual regression tests under any QPA, including
     * @c offscreen.
     *
     * @return The rendered QImage, or a null QImage if no backend can be
     *         created on this machine.
     */
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
