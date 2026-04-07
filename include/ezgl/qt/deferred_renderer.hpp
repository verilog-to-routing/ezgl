#pragma once

#ifdef EZGL_QT

#include "ezgl/graphics.hpp"
#include "ezgl/qt/painter.hpp"

#include <QLineF>
#include <QRectF>
#include <unordered_map>
#include <vector>

namespace ezgl {

// ---- style keys ----------------------------------------------------------

// Packed key for line primitives: color(32) | line_width(16) | line_cap(8) | line_dash(8)
struct LineStyleKey {
    uint32_t color_rgba;   // r | g<<8 | b<<16 | a<<24
    uint16_t line_width;   // current_line_width clamped to uint16
    uint8_t  line_cap;     // line_cap enum cast to uint8
    uint8_t  line_dash;    // line_dash enum cast to uint8

    uint64_t key() const {
        return uint64_t(color_rgba)
             | (uint64_t(line_width) << 32)
             | (uint64_t(line_cap)   << 48)
             | (uint64_t(line_dash)  << 56);
    }
};

// Packed key for filled rectangles: only color matters (no stroke attributes)
struct FillStyleKey {
    uint32_t color_rgba;

    uint64_t key() const { return color_rgba; }
};

// ---- batch storage -------------------------------------------------------

struct LineBatch {
    LineStyleKey        style;
    std::vector<QLineF> lines;
};

struct FillRectBatch {
    FillStyleKey        style;
    std::vector<QRectF> rects;
};

struct DrawRectBatch {
    LineStyleKey        style;
    std::vector<QRectF> rects;
};

// ---- deferred_renderer ---------------------------------------------------

class deferred_renderer : public renderer {
public:
    deferred_renderer(Painter *painter,
                      transform_fn transform,
                      camera *cam,
                      QImage *surface);

    // Hot-path overrides — collect into batches instead of drawing immediately.
    void draw_line(point2d start, point2d end) override;

    void fill_rectangle(point2d start, point2d end) override;
    void fill_rectangle(point2d start, double width, double height) override;
    void fill_rectangle(rectangle r) override;

    void draw_rectangle(point2d start, point2d end) override;
    void draw_rectangle(point2d start, double width, double height) override;
    void draw_rectangle(rectangle r) override;

    // Flush all batches to the underlying QPainter, then reset.
    void flush();

private:
    void reset();

    LineStyleKey current_line_style() const;
    FillStyleKey current_fill_style() const;

    void add_line(const LineStyleKey &s, QLineF line);
    void add_fill_rect(const FillStyleKey &s, QRectF rect);
    void add_draw_rect(const LineStyleKey &s, QRectF rect);

    QRectF to_screen_rect(point2d start, point2d end);

    // Batch vectors — maintain submission order for painter's algorithm.
    std::vector<LineBatch>     m_line_batches;
    std::vector<FillRectBatch> m_fill_rect_batches;
    std::vector<DrawRectBatch> m_draw_rect_batches;

    // Fast lookup: style key → index into the vectors above.
    std::unordered_map<uint64_t, size_t> m_line_idx;
    std::unordered_map<uint64_t, size_t> m_fill_rect_idx;
    std::unordered_map<uint64_t, size_t> m_draw_rect_idx;
};

} // namespace ezgl

#endif // EZGL_QT
