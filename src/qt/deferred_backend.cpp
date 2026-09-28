#include "ezgl/qt/deferred_backend.hpp"
#include "ezgl/qt/painter.hpp"
#include "ezgl/qt/deferred_renderer.hpp"
#include "ezgl/qt/immediate_renderer.hpp"
#include "ezgl/qt/drawingareawidget.hpp"
#include "ezgl/logutils.hpp"

#include <functional>

namespace ezgl {

deferred_backend::deferred_backend(QWidget*       drawing_area,
                                   draw_canvas_fn draw_callback,
                                   camera*        cam,
                                   color          background_color)
    : m_drawing_area(drawing_area)
    , m_draw_callback(draw_callback)
    , m_camera(cam)
    , m_background_color(background_color)
{
    recreate_surface();
}

deferred_backend::~deferred_backend()
{
    delete m_painter;
    delete m_animation_renderer;
}

void deferred_backend::recreate_surface()
{
    DrawingAreaWidget* daw = qobject_cast<DrawingAreaWidget*>(m_drawing_area);
    if (!daw)
        return;

    m_surface = daw->replaceSurface();
    if (!m_surface)
        return;

    delete m_painter;
    m_painter = new Painter(m_surface);
    m_painter->setAntialias(false);
    m_painter->setSmoothPixmap(false);
}

void deferred_backend::redraw()
{
    if (!m_painter)
        return;

    m_painter->set_source_rgb(m_background_color.red   / 255.0,
                              m_background_color.green / 255.0,
                              m_background_color.blue  / 255.0);
    m_painter->paint();

    deferred_renderer g(m_painter, m_camera);
    m_draw_callback(&g);
    g.flush();

    m_drawing_area->update();
    q_debug("The canvas will be redrawn (deferred path).");
}

void deferred_backend::redraw_at_view_change(view_change_reason /*reason*/)
{
    redraw();
}

void deferred_backend::on_resize(int /*w*/, int /*h*/)
{
    if (m_painter) {
        delete m_painter;
        m_painter = nullptr;
    }
    recreate_surface();
    if (!m_painter)
        return;
    redraw();
    if (m_animation_renderer)
        m_animation_renderer->set_painter(m_painter);
}

renderer* deferred_backend::create_animation_renderer()
{
    if (!m_animation_renderer) {
        m_animation_renderer = new immediate_renderer(m_painter, m_camera);
    }
    return m_animation_renderer;
}

QImage deferred_backend::render_to_image(int w, int h)
{
    QImage surface(w, h, QImage::Format_ARGB32);
    Painter painter(&surface);
    painter.set_source_rgb(m_background_color.red   / 255.0,
                           m_background_color.green / 255.0,
                           m_background_color.blue  / 255.0);
    painter.paint();

    deferred_renderer g(&painter, m_camera);
    m_draw_callback(&g);
    g.flush();
    return surface;
}

} // namespace ezgl
