# Changelog

All notable changes to EZGL will be documented in this file.

## [v1.1.0] - 2026-05-22

### Added
- PDF, PNG, and SVG export support via `print_pdf`, `print_png`, and `print_svg` on the canvas
- Scaling factor and justification options for PNG surface exports
- Animation support without requiring multi-threading
- Ability to re-enter the GTK event loop after quitting
- Ability to save graphics output without entering the event loop
- GTK application ID option to allow multiple application instances to run simultaneously
- `get_renderer()` and `flush_drawing()` functions on the application
- World coordinate and visible bounds setter functions on the canvas
- Functions to get the visible world and screen coordinates of a rectangle
- Dialog window, message popup, label creation, and widget find/delete utility functions
- Keyboard events are now correctly routed to the canvas (via `GtkEventBox`)
- Background color setter for the canvas
- Default constructors for `point2d`, `color`, and `rectangle`
- Constructors for `application::settings`
- Rotation angle and surface creation validation checks
- MinGW/Windows build support
- Support for GTK versions older than 3.20
- HiDPI display support (surface scaling factor is now fixed to 1)
- CI/CD pipeline using GitHub Actions, building on Ubuntu 22.04 and 24.04 in both release and debug modes

### Changed
- **Breaking**: C++ standard bumped to C++20; a C++20-compatible compiler is now required
- **Breaking**: `get_renderer()` now returns a pointer instead of a copy, and `draw_main_canvas` passes the renderer by pointer
- Default mouse button for canvas panning changed from right-click to left-click
- CMake minimum version bumped from 3.0.2 to 3.10
- `M_PI` replaced with `std::numbers::pi` (C++20)

### Fixed
- Fixed memory leaks in `basic_application`
- Fixed spurious left-click events firing when the user pans and releases the mouse
- Fixed user-defined mouse-press callback being suppressed while in panning mode
- Fixed panning being artificially rate-limited to 10 Hz
- Fixed a coordinate bug in `set_visible_world`
- Fixed `draw_text` bounds checking to use the current coordinate system, consistent with other drawing functions
- Fixed a justification bug in `draw_text`
- Fixed pixel clipping for large pixel coordinates
- Fixed non-root CMake project variable condition (`IS_ROOT_PROJECT`)
- Fixed line drawing rendering artifacts
- Fixed multiple compiler warnings in `graphics.cpp`, `canvas.cpp`, and `rectangle.hpp`

### Infra
- Added `.gitignore`
- Added `.clang-format`
- Added `CHANGELOG.md` and `RELEASE.md`

## [v1.0.0] - 2019-08-19

### Added
- Initial release of EZGL
- GTK3 and Cairo-backed application and canvas framework
- 2D drawing primitives (lines, rectangles, arcs, text, images)
- Camera and world-coordinate system
- Mouse and keyboard event callbacks
- Basic application example
