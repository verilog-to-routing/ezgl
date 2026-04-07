#include "ezgl/spatial_grid.hpp"

#include <algorithm>
#include <cmath>

namespace ezgl {

// ---- helpers ----------------------------------------------------------------

int SpatialGrid::cell_col(double x) const
{
    int c = int((x - m_bounds.left()) / m_cell_w);
    return std::max(0, std::min(c, m_cols - 1));
}

int SpatialGrid::cell_row(double y) const
{
    int r = int((y - m_bounds.bottom()) / m_cell_h);
    return std::max(0, std::min(r, m_rows - 1));
}

// ---- build ------------------------------------------------------------------

void SpatialGrid::build(const point2d* starts,
                        const point2d* ends,
                        std::size_t    count,
                        double         cell_size)
{
    m_seg_count = count;
    if (count == 0) return;

    // Compute world bounding box of all segments.
    double min_x = starts[0].x, max_x = starts[0].x;
    double min_y = starts[0].y, max_y = starts[0].y;
    for (std::size_t i = 0; i < count; ++i) {
        min_x = std::min({min_x, starts[i].x, ends[i].x});
        max_x = std::max({max_x, starts[i].x, ends[i].x});
        min_y = std::min({min_y, starts[i].y, ends[i].y});
        max_y = std::max({max_y, starts[i].y, ends[i].y});
    }
    // Add a small margin to avoid edge-case indexing.
    min_x -= 1e-6; min_y -= 1e-6;
    max_x += 1e-6; max_y += 1e-6;

    m_bounds  = rectangle{{min_x, min_y}, {max_x, max_y}};
    m_cell_w  = cell_size;
    m_cell_h  = cell_size;
    m_cols    = std::max(1, int(std::ceil(m_bounds.width()  / m_cell_w)));
    m_rows    = std::max(1, int(std::ceil(m_bounds.height() / m_cell_h)));

    m_cells.clear();
    m_cells.resize(std::size_t(m_cols) * std::size_t(m_rows));

    for (std::size_t i = 0; i < count; ++i) {
        // AABB of segment i.
        int c0 = cell_col(std::min(starts[i].x, ends[i].x));
        int c1 = cell_col(std::max(starts[i].x, ends[i].x));
        int r0 = cell_row(std::min(starts[i].y, ends[i].y));
        int r1 = cell_row(std::max(starts[i].y, ends[i].y));
        for (int r = r0; r <= r1; ++r)
            for (int c = c0; c <= c1; ++c)
                m_cells[std::size_t(cell_index(c, r))].indices.push_back(i);
    }

    // Generation table for deduplication in query().
    m_gen.assign(count, 0);
    m_cur_gen = 1; // first real query will use gen 1
}

// ---- query ------------------------------------------------------------------

std::vector<std::size_t> SpatialGrid::query(const rectangle& viewport) const
{
    if (m_cells.empty()) return {};

    const std::size_t gen = ++m_cur_gen;

    int c0 = cell_col(viewport.left());
    int c1 = cell_col(viewport.right());
    int r0 = cell_row(viewport.bottom());
    int r1 = cell_row(viewport.top());

    // Clamp to grid.
    c0 = std::max(0, c0); c1 = std::min(m_cols - 1, c1);
    r0 = std::max(0, r0); r1 = std::min(m_rows - 1, r1);

    std::vector<std::size_t> result;
    result.reserve(std::size_t((c1 - c0 + 1) * (r1 - r0 + 1)) * 4);

    for (int r = r0; r <= r1; ++r) {
        for (int c = c0; c <= c1; ++c) {
            const Cell& cell = m_cells[std::size_t(cell_index(c, r))];
            for (std::size_t idx : cell.indices) {
                if (m_gen[idx] != gen) {
                    m_gen[idx] = gen;
                    result.push_back(idx);
                }
            }
        }
    }
    return result;
}

} // namespace ezgl
