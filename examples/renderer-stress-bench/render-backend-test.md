# Render Backend Test

ezgl supports three render paths covered by this test: immediate, deferred, and
RHI.

This test compares ezgl render backend performance on a complex VPR-like scene.

## Test Scenario

- Build configuration: `RelWithDebInfo`
- Benchmark target: ezgl headless rendering of a complex VPR-like scene
- Measured time: full headless render call, including geometry loading
- Primitive counts: extracted from VPR runtime debug information and recreated in
  the ezgl renderer stress benchmark

| Primitive set | Count | Notes |
| --- | ---: | --- |
| Thin lines | 71,554,146 | `thin_verts / 2` |
| Filled rectangles | 115,482 |  |
| Filled polygons | 34,943,940 | Routing arrowheads, represented as 3-point triangles |
| Dashed lines | 443,724 |  |

## Test Machine

- Form factor: laptop
- CPU: Intel Core i9-9980H
- GPU: NVIDIA Quadro RTX 4000
- VRAM: 4 GB
- RAM: 32 GB
- OS: Ubuntu 20.04

## Results

| Renderer | Time | Notes |
| --- | ---: | --- |
| Immediate renderer, software | 49.5 s | Full render |
| Deferred renderer, batched software | 24.7 s | Full render |
| RHI renderer, batched hardware | 17.9 s | First frame |
| RHI renderer, batched hardware | <200 ms | Subsequent frames after camera changes |

## Notes

The RHI renderer bakes and uploads geometry to the GPU on the first frame. On
subsequent frames, camera transformations can be applied without rebuilding the
full scene.

While baking geometry for the GPU, the RHI renderer uses multithreaded CPU data
processing. The immediate and deferred renderers use a single CPU thread for
their render path in this test. As a result, RHI first-frame load time may vary
relative to the immediate and deferred modes depending on the available CPU core
count.

This benchmark does not directly compare those subsequent-frame gains against
the immediate and deferred renderers, because those renderers rebuild the full
scene after camera transformation changes.
