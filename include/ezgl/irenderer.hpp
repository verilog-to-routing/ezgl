#pragma once

#include "ezgl/color.hpp"
#include "ezgl/point.hpp"
#include "ezgl/rectangle.hpp"

#include <QFont>
#include <Qt>
#include <QImage>

#include <cstdint>
#include <string>
#include <vector>

namespace ezgl {

/**
 * An in-memory image (QImage) that can be blitted onto the canvas via
 * draw_surface(). Name retained from the original Cairo-based ezgl API.
 */
typedef QImage surface;

class camera;
class Painter;

/** Coordinate space a draw call is expressed in: WORLD units or SCREEN pixels. */
enum t_coordinate_system { WORLD, SCREEN };

/** How text is anchored relative to its draw point, horizontally and vertically. */
enum class justification { center, left, right, top, bottom };

/** Font posture, mapping onto the corresponding QFont styles. */
enum class font_slant : int {
    normal  = QFont::StyleNormal,
    italic  = QFont::StyleItalic,
    oblique = QFont::StyleOblique
};

/** Font weight, mapping onto the corresponding QFont weights. */
enum class font_weight : int {
    normal = QFont::Normal,
    bold   = QFont::Bold
};

/** How line ends are drawn, mapping onto Qt's pen cap styles. */
enum class line_cap : int {
    butt  = Qt::FlatCap,
    round = Qt::RoundCap
};

/** Line dash pattern: solid (none) or a repeating 5-on/3-off dash. */
enum class line_dash : int {
    none,
    asymmetric_5_3
};

/**
 * @brief Specifies a string's width and height in pixels.
 */
struct text_dimension_t {
    double width = 0.0;
    double height = 0.0;
};

/**
 * Base interface and shared state for all ezgl renderers.
 *
 * Shared state setters and camera helpers live here. Concrete renderers own
 * the draw-call behavior they batch, accelerate, or paint immediately.
 */
class irenderer {
public:
    virtual ~irenderer() = default;

    /**
     * Select whether subsequent draw calls interpret coordinates as WORLD
     * units or SCREEN pixels.
     *
     * @param new_coordinate_system WORLD to draw in world units (mapped through
     *                              the camera), SCREEN to draw in raw pixels.
     */
    virtual void set_coordinate_system(t_coordinate_system new_coordinate_system);

    /**
     * Set the world-coordinate region mapped onto the canvas (i.e. the
     * pan/zoom view). The region is adjusted to preserve the initial world
     * aspect ratio, so the result may be larger than requested.
     *
     * @param new_world Desired visible region in world coordinates.
     */
    virtual void set_visible_world(rectangle new_world);

    /**
     * @return The visible canvas area in world coordinates.
     */
    virtual rectangle get_visible_world();

    /**
     * @return The visible canvas area in screen-pixel coordinates.
     */
    virtual rectangle get_visible_screen() const;

    /**
     * Map a world-coordinate rectangle to its screen-pixel bounds.
     *
     * @param box Rectangle in world coordinates.
     * @return The corresponding rectangle in screen-pixel coordinates.
     */
    virtual rectangle world_to_screen(const rectangle& box);
    /// Return the horizontal world-space distance represented by one screen pixel.
    double world_units_per_pixel();

    /**
     * Set the active draw color for subsequent strokes and fills.
     *
     * @param new_color RGBA color; its alpha is used as-is.
     */
    virtual void set_color(color new_color);

    /**
     * Set the active draw color, overriding its alpha.
     *
     * @param new_color RGB color (its own alpha is ignored).
     * @param alpha     Opacity to apply, 0 (transparent) to 255 (opaque).
     */
    virtual void set_color(color new_color, uint_fast8_t alpha);

    /**
     * Set the active draw color from individual 0-255 RGBA components.
     *
     * @param red   Red channel, 0-255.
     * @param green Green channel, 0-255.
     * @param blue  Blue channel, 0-255.
     * @param alpha Opacity, 0 (transparent) to 255 (opaque); defaults to opaque.
     */
    virtual void set_color(uint_fast8_t red, uint_fast8_t green, uint_fast8_t blue,
                           uint_fast8_t alpha = 255);

    /**
     * Set how line ends are drawn.
     *
     * @param cap line_cap::butt for flat ends or line_cap::round for rounded ends.
     */
    virtual void set_line_cap(line_cap cap);

    /**
     * Set the line dash pattern.
     *
     * @param dash line_dash::none for a solid line or line_dash::asymmetric_5_3
     *             for a repeating 5-on/3-off dash.
     */
    virtual void set_line_dash(line_dash dash);

    /**
     * Set the line width for subsequent strokes.
     *
     * @param width Width in pixels; a width of 0 is treated as 1.
     */
    virtual void set_line_width(int width);

    /**
     * Set the font size for subsequent text.
     *
     * @param new_size Font size in pixels.
     */
    virtual void set_font_size(double new_size);

    /**
     * Select the font face for subsequent text.
     *
     * @param family Font family name (e.g. "sans").
     * @param slant  Font posture (normal, italic, or oblique).
     * @param weight Font weight (normal or bold).
     */
    virtual void format_font(std::string const& family, font_slant slant, font_weight weight);

    /**
     * Select the font face and size for subsequent text.
     *
     * @param family   Font family name (e.g. "sans").
     * @param slant    Font posture (normal, italic, or oblique).
     * @param weight   Font weight (normal or bold).
     * @param new_size Font size in pixels.
     */
    virtual void format_font(std::string const& family, font_slant slant,
                             font_weight weight, double new_size);

    /**
     * Set the rotation applied to subsequent text.
     *
     * @param degrees Rotation in degrees, counter-clockwise. Values outside
     *                [-360, 360] are rejected and the call is ignored.
     */
    virtual void set_text_rotation(double degrees);

    /**
     * Set horizontal anchoring of text relative to its draw point.
     *
     * @param horiz_just justification::center, ::left, or ::right;
     *                   ::top and ::bottom are ignored.
     */
    virtual void set_horiz_justification(justification horiz_just);

    /**
     * Set vertical anchoring of text relative to its draw point.
     *
     * @param vert_just justification::center, ::top, or ::bottom;
     *                  ::left and ::right are ignored.
     */
    virtual void set_vert_justification(justification vert_just);

    /**
     * Set a one-shot screen-pixel offset to be applied to the next
     * draw_text call. The offset is added AFTER the world→screen
     * transform, so its visible distance is constant in screen pixels at
     * every zoom level — useful for placing labels just off a line drawn
     * in WORLD coords (e.g. critical-path delay annotations) without the
     * label drifting on zoom under the camera-only redraw path.
     *
     * The offset auto-resets to (0,0) once consumed by the next draw_text.
     */
    virtual void set_text_screen_offset(point2d offset_px);

    /**
     * Draw a straight line between two points, using the current color, line
     * width, cap, and dash. Coordinates are interpreted in the current
     * coordinate system (WORLD or SCREEN).
     *
     * @param start One endpoint of the line.
     * @param end   The other endpoint of the line.
     */
    virtual void draw_line(const point2d& start, const point2d& end) = 0;

    /**
     * Draw a rectangle outline given two opposite corners.
     *
     * @param start One corner of the rectangle.
     * @param end   The opposite corner.
     */
    virtual void draw_rectangle(const point2d& start, const point2d& end) = 0;

    /**
     * Draw a rectangle outline given one corner and its size.
     *
     * @param start  The origin corner of the rectangle.
     * @param width  Width of the rectangle.
     * @param height Height of the rectangle.
     */
    virtual void draw_rectangle(const point2d& start, double width, double height) = 0;

    /**
     * Draw the outline of a rectangle.
     *
     * @param r The rectangle to draw.
     */
    virtual void draw_rectangle(const rectangle& r) = 0;

    /**
     * Draw a filled rectangle given two opposite corners.
     *
     * @param start One corner of the rectangle.
     * @param end   The opposite corner.
     */
    virtual void fill_rectangle(const point2d& start, const point2d& end) = 0;

    /**
     * Draw a filled rectangle given one corner and its size.
     *
     * @param start  The origin corner of the rectangle.
     * @param width  Width of the rectangle.
     * @param height Height of the rectangle.
     */
    virtual void fill_rectangle(const point2d& start, double width, double height) = 0;

    /**
     * Draw a filled rectangle.
     *
     * @param r The rectangle to fill.
     */
    virtual void fill_rectangle(const rectangle& r) = 0;

    /**
     * Draw a filled polygon defined by its vertices, in order.
     *
     * @param points The polygon's vertices; the outline is implicitly closed
     *               from the last vertex back to the first.
     */
    virtual void fill_poly(const std::vector<point2d>& points) = 0;

    /**
     * Draw a filled triangle defined by its three vertices.
     *
     * @param a First vertex.
     * @param b Second vertex.
     * @param c Third vertex.
     */
    virtual void fill_triangle(const point2d& a, const point2d& b, const point2d& c) = 0;

    /**
     * Fill an arrow-head triangle anchored to a world position but rendered
     * at a constant SCREEN size at every zoom level.
     *
     * @param anchor_world  World position of the arrow's anchor point.
     * @param dir_world     Direction the arrow points, in world coords. Any
     *                      nonzero length — the implementation normalises
     *                      before computing the arrow geometry.
     * @param arrow_size_px Arrow tip-to-tip size in screen pixels.
     *
     * The default implementation expands the arrow into world-coord vertices
     * using the camera's current world-scale and calls fill_triangle — the
     * right behaviour for the immediate backend (no zoom-time updates).
     * Deferred and RHI override this to keep the on-screen size invariant
     * under camera-only redraws and at any zoom level. RHI uploads one
     * GPU instance per call and synthesises the triangle in a vertex
     * shader; deferred captures per-vertex pixel offsets and replays them.
     */
    virtual void fill_arrow_pointer_triangle(const point2d& anchor_world,
                                              const point2d& dir_world,
                                              float          arrow_size_px);
    /**
     * Draw the outline of an elliptic arc.
     *
     * @param center      Center of the ellipse.
     * @param radius_x    Horizontal radius.
     * @param radius_y    Vertical radius.
     * @param start_angle Angle at which the arc begins, in degrees measured
     *                    counter-clockwise from the positive x-axis.
     * @param extent_angle Angular sweep of the arc, in degrees; positive sweeps
     *                     counter-clockwise, negative clockwise.
     */
    virtual void draw_elliptic_arc(const point2d& center, double radius_x, double radius_y,
                                   double start_angle, double extent_angle) = 0;

    /**
     * Draw the outline of a circular arc.
     *
     * @param center      Center of the arc.
     * @param radius      Radius of the arc.
     * @param start_angle Angle at which the arc begins, in degrees measured
     *                    counter-clockwise from the positive x-axis.
     * @param extent_angle Angular sweep of the arc, in degrees; positive sweeps
     *                     counter-clockwise, negative clockwise.
     */
    virtual void draw_arc(const point2d& center, double radius,
                          double start_angle, double extent_angle) = 0;

    /**
     * Draw a filled elliptic arc (a pie/wedge bounded by the arc and its
     * radii).
     *
     * @param center      Center of the ellipse.
     * @param radius_x    Horizontal radius.
     * @param radius_y    Vertical radius.
     * @param start_angle Angle at which the arc begins, in degrees measured
     *                    counter-clockwise from the positive x-axis.
     * @param extent_angle Angular sweep of the arc, in degrees; positive sweeps
     *                     counter-clockwise, negative clockwise.
     */
    virtual void fill_elliptic_arc(const point2d& center, double radius_x, double radius_y,
                                   double start_angle, double extent_angle) = 0;

    /**
     * Draw a filled circular arc (a pie/wedge bounded by the arc and its
     * radii).
     *
     * @param center      Center of the arc.
     * @param radius      Radius of the arc.
     * @param start_angle Angle at which the arc begins, in degrees measured
     *                    counter-clockwise from the positive x-axis.
     * @param extent_angle Angular sweep of the arc, in degrees; positive sweeps
     *                     counter-clockwise, negative clockwise.
     */
    virtual void fill_arc(const point2d& center, double radius,
                          double start_angle, double extent_angle) = 0;

    /**
     * Draw text anchored at a point. The text is positioned relative to the
     * point according to the current horizontal and vertical justification,
     * and uses the current color, font, and text rotation.
     *
     * @param point Anchor position for the text.
     * @param text  The string to draw.
     */
    virtual void draw_text(const point2d& point, std::string const& text) = 0;

    /**
     * Draw text anchored at a point, but only if it fits within the given
     * bounds. If the rendered text is wider than @p bound_x or taller than
     * @p bound_y, nothing is drawn.
     *
     * @param point   Anchor position for the text.
     * @param text    The string to draw.
     * @param bound_x Maximum text width; pass a non-finite value (or DBL_MAX)
     *                to leave the width unbounded.
     * @param bound_y Maximum text height; pass a non-finite value (or DBL_MAX)
     *                to leave the height unbounded.
     */
    virtual void draw_text(const point2d& point, std::string const& text,
                           double bound_x, double bound_y) = 0;

    /**
     * Draw a previously loaded image surface, anchored at a point according to
     * the current justification.
     *
     * @param p_surface    The surface to draw (see load_png).
     * @param anchor_point Position the surface is anchored to.
     * @param scale_factor Uniform scale applied to the surface; 1 draws it at
     *                     its native size.
     */
    virtual void draw_surface(surface* p_surface, const point2d& anchor_point,
                              double scale_factor = 1) = 0;

    /**
     * Load a PNG image from disk into a surface that can be drawn with
     * draw_surface. The caller owns the returned surface and must release it
     * with free_surface.
     *
     * @param file_path Path to the PNG file.
     * @return A newly allocated surface; never null, but the load is only
     *         logged (not signalled) on failure, so a missing or invalid file
     *         yields an empty surface.
     */
    static surface* load_png(const char* file_path);

    /**
     * Free a surface previously returned by load_png.
     *
     * @param p_surface The surface to release.
     */
    static void free_surface(surface* p_surface);

    /**
     * Rebind this renderer to a (possibly new) painter and re-apply the cached
     * font, color, line width, cap, and dash to it. Called by the backends
     * after the render target is recreated (e.g. on resize).
     *
     * @param painter The painter to draw through.
     */
    void set_painter(Painter* painter);

    /**
     * @brief Returns the dimension (width and height in pixels) of a string.
     *
     * @param text The string to calculate.
     * @return A struct that contains two doubles specifying the string's width and height.
     */
    text_dimension_t get_text_dimension(const std::string& text);

protected:
    /**
     * Construct the renderer base with the painter and camera that subclasses
     * draw through.
     *
     * @param painter Backend painter used to issue draw primitives.
     * @param cam     Camera supplying the current view, scale, and the
     *                world→screen transform used to map draw coordinates.
     */
    irenderer(Painter* painter, camera* cam);

    Painter*            m_painter{nullptr};        ///< Backend painter draw calls are issued to.
    camera*             m_camera{nullptr};         ///< Camera providing the view, scale, and world→screen transform.
    t_coordinate_system current_coordinate_system = WORLD; ///< How draw-call coordinates are interpreted.
    color               current_color{0, 0, 0, 255};       ///< Active stroke/fill color.
    int                 current_line_width  = 0;   ///< Active line width in pixels (0 means 1).
    line_cap            current_line_cap    = line_cap::butt;   ///< Active line-cap style.
    line_dash           current_line_dash   = line_dash::none;  ///< Active line-dash pattern.
    double              rotation_angle      = 0.0; ///< Text rotation in radians (clockwise-negated degrees).
    justification       horiz_justification = justification::center; ///< Horizontal text/surface anchoring.
    justification       vert_justification  = justification::center; ///< Vertical text/surface anchoring.
    QFont               current_font;              ///< Active font for text.
    point2d             text_screen_offset_px = {0.0, 0.0}; ///< One-shot screen offset for the next draw_text.

    /// Readability cull threshold for WORLD text: a vertical text bound that
    /// maps to fewer than this many screen pixels at the current zoom is too
    /// small to read and the label is skipped (see
    /// @ref deferred_renderer::world_text_visible).
    static constexpr double MINIMAL_VISIBLE_TEXT_BOUND_Y_IN_PX = 5.0;

    /**
     * Test whether a rectangle lies entirely outside the visible world, used
     * to cull off-screen primitives. Always returns false in SCREEN
     * coordinates (no culling).
     *
     * @param rect Rectangle to test, in the current coordinate system.
     * @return True if @p rect is fully outside the visible world.
     */
    bool rectangle_off_screen(rectangle rect);

    /**
     * Clip a line segment to a world-coordinate window (Cohen–Sutherland),
     * updating the endpoints in place.
     *
     * @param clip_window Window to clip against, in world coordinates.
     * @param start       Line start; modified to the clipped position.
     * @param end         Line end; modified to the clipped position.
     * @return True if any part of the line is visible, false if fully clipped.
     */
    bool clip_line_world(const rectangle& clip_window, point2d& start, point2d& end);

    /**
     * Stroke a line, culling and (in WORLD coords) clipping it to the visible
     * world before transforming to screen space.
     */
    void paint_line(const point2d& start, const point2d& end);

    /**
     * Build a rectangle path from two opposite corners and either fill or
     * stroke it.
     *
     * @param start One corner.
     * @param end   The opposite corner.
     * @param fill  True to fill, false to stroke the outline.
     */
    void paint_rectangle_path(const point2d& start, const point2d& end, bool fill);

    /**
     * Fill a closed polygon, culling it if its bounding box is off-screen.
     *
     * @param points Polygon vertices in order (must contain at least two).
     */
    void paint_poly(const std::vector<point2d>& points);

    /**
     * Build and fill or stroke an arc path.
     *
     * @param center        Center of the arc.
     * @param radius        Base radius (the x-radius).
     * @param start_angle   Start angle in degrees, counter-clockwise from +x.
     * @param extent_angle  Angular sweep in degrees.
     * @param stretch_factor Vertical scale applied to @p radius to produce an
     *                       ellipse (1 for a circle).
     * @param fill          True to fill the wedge, false to stroke the arc.
     */
    void paint_arc_path(const point2d& center, double radius, double start_angle,
                       double extent_angle, double stretch_factor, bool fill);

    /**
     * Render a text string, applying justification, rotation, and the one-shot
     * screen offset, and skipping it if it exceeds the given bounds.
     *
     * @param point   Anchor position for the text.
     * @param text    The string to draw.
     * @param bound_x Maximum width, or non-finite/DBL_MAX for unbounded.
     * @param bound_y Maximum height, or non-finite/DBL_MAX for unbounded.
     */
    void paint_text(const point2d& point, const std::string& text,
                    double bound_x, double bound_y);

    /**
     * Draw an image surface, applying justification and scaling.
     *
     * @param p_surface   The surface to draw.
     * @param anchor      Anchor position for the surface.
     * @param scale_factor Uniform scale applied to the surface.
     */
    void paint_surface(surface* p_surface, const point2d& anchor, double scale_factor);
};

/**
 * Public name for the renderer interface. Client draw callbacks take an
 * @c ezgl::renderer* (e.g. `void draw(ezgl::renderer* g)`) without depending
 * on the @c irenderer interface name or any concrete backend
 * (immediate/deferred/RHI).
 */
using renderer = irenderer;

} // namespace ezgl
