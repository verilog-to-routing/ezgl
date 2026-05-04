#include "ezgl/qt/immediate_backend.hpp"
#include "ezgl/qt/painter.hpp"
#include "ezgl/qt/immediate_renderer.hpp"
#include "ezgl/qt/drawingareawidget.hpp"
#include "ezgl/logutils.hpp"

#include <functional>

namespace ezgl {

immediate_backend::immediate_backend(QWidget*       drawing_area,
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

immediate_backend::~immediate_backend()
{
    delete m_renderer;
    delete m_painter;
}

void immediate_backend::recreate_surface()
{
    DrawingAreaWidget* daw = qobject_cast<DrawingAreaWidget*>(m_drawing_area);
    if (!daw)
        return;

    m_surface = daw->replaceSurface();
    if (!m_surface)
        return;

    delete m_renderer;
    m_renderer = nullptr;
    delete m_painter;

    m_painter = new Painter(m_surface);
    m_painter->setAntialias(false);
    m_painter->setSmoothPixmap(false);

    using namespace std::placeholders;
    m_renderer = new immediate_renderer(
        m_painter,
        std::bind(&camera::world_to_screen, m_camera, _1),
        m_camera,
        m_surface);
}

void immediate_backend::redraw()
{
    if (!m_painter || !m_renderer)
        return;

    m_painter->set_source_rgb(m_background_color.red   / 255.0,
                              m_background_color.green / 255.0,
                              m_background_color.blue  / 255.0);
    m_painter->paint();

    m_draw_callback(m_renderer);

    m_drawing_area->update();
    q_debug("The canvas will be redrawn (immediate path).");
}

void immediate_backend::redraw_camera_only()
{
    redraw();
}

void immediate_backend::on_resize(int /*w*/, int /*h*/)
{
    if (m_painter) {
        delete m_renderer;
        m_renderer = nullptr;
        delete m_painter;
        m_painter = nullptr;
    }
    recreate_surface();
    if (!m_painter)
        return;
    redraw();
}

renderer* immediate_backend::create_animation_renderer()
{
    return m_renderer;
}

QImage immediate_backend::render_to_image(int w, int h)
{
    QImage surface(w, h, QImage::Format_ARGB32);
    Painter painter(&surface);
    painter.set_source_rgb(m_background_color.red   / 255.0,
                           m_background_color.green / 255.0,
                           m_background_color.blue  / 255.0);
    painter.paint();

    using namespace std::placeholders;
    immediate_renderer g(&painter, std::bind(&camera::world_to_screen, *m_camera, _1), m_camera, &surface);
    m_draw_callback(&g);
    return surface;
}

} // namespace ezgl
