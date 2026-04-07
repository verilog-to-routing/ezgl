// #pragma once

// #include "ezgl/point.hpp"
// #include "ezgl/rectangle.hpp"

// #include <cstddef>
// #include <vector>

// namespace ezgl {

// /**
//  * Uniform spatial grid for fast viewport culling of 2D line segments.
//  *
//  * Usage:
//  *   SpatialGrid grid;
//  *   grid.build(starts, ends, count, cell_size);
//  *   // each frame:
//  *   auto visible = grid.query(viewport);
//  *   for (size_t idx : visible) { draw(starts[idx], ends[idx]); }
//  *
//  * Complexity:
//  *   build:  O(N) amortised (each segment touches a constant number of cells
//  *           for typical axis-aligned routing geometry)
//  *   query:  O(k + cells_in_viewport) where k = returned segment count
//  */
// class SpatialGrid {
// public:
//     /**
//      * Build the index.
//      *
//      * @param starts   Array of segment start points (world coords).
//      * @param ends     Array of segment end points (world coords).
//      * @param count    Number of segments (starts[0..count-1], ends[0..count-1]).
//      * @param cell_size Grid cell side length in world units.
//      *                  A good heuristic: world_size / sqrt(count).
//      */
//     void build(const point2d* starts,
//                const point2d* ends,
//                std::size_t    count,
//                double         cell_size);

//     /**
//      * Return the indices of all segments whose AABB intersects [viewport].
//      *
//      * Each index appears at most once (generation-counter deduplication —
//      * no heap allocation per query).
//      */
//     std::vector<std::size_t> query(const rectangle& viewport) const;

//     /** True if build() has been called with at least one segment. */
//     bool empty() const { return m_cells.empty(); }

//     /** Number of segments stored. */
//     std::size_t segment_count() const { return m_seg_count; }

// private:
//     struct Cell {
//         std::vector<std::size_t> indices;
//     };

//     // Flat row-major cell array.
//     std::vector<Cell> m_cells;
//     rectangle         m_bounds;
//     double            m_cell_w  = 1.0;
//     double            m_cell_h  = 1.0;
//     int               m_cols    = 0;
//     int               m_rows    = 0;
//     std::size_t       m_seg_count = 0;

//     // Generation counter for O(1) seen-check during query.
//     mutable std::vector<std::size_t> m_gen;
//     mutable std::size_t              m_cur_gen = 0;

//     int  cell_col(double x) const;
//     int  cell_row(double y) const;
//     int  cell_index(int col, int row) const { return row * m_cols + col; }
// };

// } // namespace ezgl
