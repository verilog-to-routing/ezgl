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
 *
 * @note New to the graphics acronyms here (QRhi, MSAA, MVP, QPA, …)? They are
 *       defined once in the glossary at the top of @ref rhi_types.hpp.
 */
class RhiCanvasWidget : public QRhiWidget {
    Q_OBJECT
public:
    /// Construct the widget and request MSAA via
    /// @c setSampleCount(EZGL_RHI_SAMPLE_COUNT). The @ref RhiSceneRenderer and
    /// all GPU objects are created lazily later in @ref initialize(), once Qt
    /// has a live @c QRhi, so no graphics context is required here.
    explicit RhiCanvasWidget(QWidget* parent = nullptr);
    /// Destroy the widget. GPU resources are torn down by @ref releaseResources()
    /// (invoked by the @c QRhiWidget teardown), so the destructor only cleans up
    /// the owned @ref RhiSceneRenderer and pending-frame state.
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
    /// Emitted from @ref resizeEvent() with the new widget size (device-
    /// independent pixels) so the backend can rebuild its camera / MVP.
    void resized(int w, int h);

protected:
    /// @c QRhiWidget hook (render thread): lazily create the
    /// @ref RhiSceneRenderer and its GPU pipelines against the now-available
    /// @c QRhi and render-pass descriptor. Called once when the QRhi becomes
    /// ready, and again after a device loss / re-initialization.
    void initialize(QRhiCommandBuffer* cb) override;
    /// @c QRhiWidget hook (render thread): snapshot the pending frame state
    /// under @c m_frame_mutex, then delegate the actual GPU draw to
    /// @ref RhiSceneRenderer::render(). Called by Qt for every frame.
    void render(QRhiCommandBuffer* cb) override;
    /// @c QRhiWidget hook (render thread): release all GPU objects before the
    /// @c QRhi is destroyed, by forwarding to @ref RhiSceneRenderer::release().
    void releaseResources() override;
    /// Run the base @c QRhiWidget handler, then emit @ref resized() with the new
    /// size (when non-empty) so the backend updates its camera / MVP.
    void resizeEvent(QResizeEvent* e) override;
    /// Run the base @c QRhiWidget handler, then emit @ref resized() with the
    /// current size (when non-empty) to drive the initial camera sizing on first
    /// show, before any resize event has fired.
    void showEvent(QShowEvent* e) override;

private:
    // ---- GPU rendering (all pipeline/frame-resource state lives here) -------
    std::unique_ptr<RhiSceneRenderer> m_scene_renderer; ///< Owns all GPU pipelines/buffers; created lazily in initialize().

    // ---- Pending frame state (written by rhi_renderer, read by render()) ----
    mutable QMutex                       m_frame_mutex;           ///< Guards every m_pending_* field and the dirty flags below.
    std::shared_ptr<const SceneBuffers>  m_pending_scene_buffers; ///< Next scene to draw; shared (not copied) with the main thread.
    QMatrix4x4                           m_pending_mvp;            ///< Next model-view-projection (MVP) matrix: the world→NDC transform.
    rectangle                            m_pending_visible_world;  ///< Next visible-world rect, used for per-chunk culling.
    QImage                               m_pending_overlay;        ///< Next QPainter overlay image (text/arcs).
    QColor                               m_pending_bg  { Qt::white }; ///< Next background clear colour.
    bool                                 m_frame_dirty = false;    ///< Set by set_frame_data(): geometry (and MVP) changed.
    bool                                 m_mvp_dirty   = false;    ///< Set by the MVP-only paths: transform/overlay changed, geometry not.
};

/**
 * Returns true if a GPU-accelerated QRhi backend can be created on this
 * machine (Metal on macOS, D3D11 on Windows, OpenGL on Linux/other). The
 * result is cached after the first call so the probe is only performed once
 * per process. Returns false on headless CI without a GPU or GPU drivers.
 */
bool probe_rhi();

} // namespace ezgl
