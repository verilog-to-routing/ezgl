# Release Process

This document describes how to create a new release of EZGL.
It is intended for project maintainers.

---

## Prerequisites

- Maintainer access to the GitHub repository
- Clean working tree
- All CI checks passing on `master`

---

## Versioning

This project follows **Semantic Versioning (SemVer)**:

- **MAJOR**: incompatible API changes
- **MINOR**: backwards-compatible new features
- **PATCH**: backwards-compatible bug fixes

Versions are prefixed with `v` in Git tags (e.g., `v1.2.3`).

---

## Pre-Release Checklist

Before cutting a release, ensure:

- [ ] CI workflow is enabled (GitHub auto-disables it after 60 days of no pushes — re-enable via the GitHub UI or the CLI)
- [ ] All CI checks are passing on `master`
- [ ] Smoke test passes (see [Smoke Test](#smoke-test) section below)
- [ ] `CHANGELOG.md` contains an entry for the new version with an accurate date
- [ ] No uncommitted changes in the working tree

---

## Smoke Test

CI only verifies that the library compiles. Because EZGL is a graphics library, visual output
must be checked manually by running the `basic-application` example.

### Build the example

```sh
cmake -H. -Bbuild -DEZGL_BUILD_EXAMPLES=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build/ --target basic-application
cd examples/basic-application
./basic_application
```

### What to verify

The application window should open and the main canvas should display all of the following:

- **Colors** — a row of filled rectangles in various named colors, plus three randomly-colored rectangles
- **Lines** — solid, dashed, and wide lines with different line caps
- **Arcs** — open and filled arcs, circular and elliptic
- **Polygons** — filled triangles and quads, including semi-transparent overlapping shapes
- **Text** — text at multiple font sizes with visible bounding boxes; text rotated at 0°, 45°, 90°, 135°, 180°, and 270°
- **Screen coordinates** — a small red rectangle fixed to the top-left corner of the window that does not move when panning or zooming
- **PNG image** — a small image rendered to the canvas

Then exercise the interactive features in the sidebar:

| Action | Expected result |
|--------|-----------------|
| Pan (left-click drag) | Canvas pans smoothly with no spurious clicks |
| Scroll to zoom | Canvas zooms in/out correctly |
| Click **Animate** | Diagonal red dashed lines animate across the canvas |
| Click **Test** | Status bar updates to `Test Button Pressed` |
| Change **Test Combo Box** selection | Status bar updates to the selected option (YES / NO / MAYBE) |
| Click **Delete Combo Box** | Combo box widget is removed from the sidebar |
| Click **Create Dialog** | A dialog window opens; accepting/rejecting/closing updates the status bar |
| Click **Create Popup Mssg** | A popup message appears |
| Press any key while canvas is focused | Status bar updates to `Key Pressed` |
| Left-click on canvas | Terminal prints the click coordinates and modifier keys |

---

## Release Steps

### 1. Update the Version Number

Update the version in `CMakeLists.txt`:

```cmake
project(
  ezgl
  VERSION 1.2.3
  LANGUAGES CXX
)
```

### 2. Update the Changelog

Fill in the date and finalize the entry in `CHANGELOG.md`:

```md
## [v1.2.3] - YYYY-MM-DD

### Added
- ...

### Fixed
- ...
```

### 3. Commit the Release

```sh
git commit -am "Release v1.2.3"
```

This can be in a PR, but the commit must be merged into `master` before tagging.

### 4. Create and Push the Git Tag

Make sure you are on a commit of `master` (i.e., you are tagging a commit that is on
the `master` branch).

```sh
git fetch origin
git checkout origin/master
git tag v1.2.3
git push --tags
```

### 5. Create the GitHub Release

Go to the GitHub Releases page and draft a new release using the tag created above.
Copy the relevant `CHANGELOG.md` section as the release description.

---

## Post-Release Checklist

- [ ] Verify the GitHub release is published correctly
- [ ] Announce the release to the VTR community

---

## Rollback Policy

Git tags should **not** be deleted or recreated.

If a release is faulty, issue a follow-up patch release instead.
