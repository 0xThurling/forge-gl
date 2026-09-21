# forge-gl performance plan (software rendering)

**Decision: forge-gl stays a CPU software renderer.** The opt-in SYCL tier in
ForgeFP (`fp/gpu.hpp`) is ForgeML's execution path; the renderer does not use
it. This document records why — with the measured numbers, not intuition — and,
more usefully, what to do instead: the CPU-side work that actually moves the
frame time.

The migration to ForgeFP ([MIGRATION.md](MIGRATION.md)) is the prerequisite for
any of this: the functional pipelines are what makes the work below small.

## Why not the GPU

Reference machine: RTX 3060 + Ryzen 5 5500, WSL2. Costs from
[`fp/GPU.md`](../fp/GPU.md#measured-crossovers-rtx-3060-wsl2):

| Cost | Value |
|---|---|
| Synchronous kernel call (launch + wait) | 0.14–0.36 ms, with multi-ms outliers |
| Elementwise crossover vs CPU | between 1 MiB and 16 MiB |
| H2D / D2H, pageable | 4.5 / 5.3 GB/s |
| `matmul` crossover | 128² |

A 640×480 RGBA framebuffer is **1.2 MB** — below the elementwise crossover, so
even the cheapest candidate (clear/post-process) loses on the target
resolution. The renderer is also a chain of small, latency-sensitive steps, and
the WSL2 launch variance would surface as frame-time stutter. The GPU is a win
for dense, large, batchable math (which is why ForgeML uses it), not for a
per-frame pipeline at these sizes.

## Where the frame time goes

| Stage | Complexity | Notes |
|---|---|---|
| Vertex transform + projection | O(vertices) | f22 ≈ 6k verts, bunny ≈ 208k verts; already `fp::par_map` |
| Painter's sort | O(faces log faces) | 2k faces (f22) → cheap; 69k (bunny) → ~1.2M comparisons/frame |
| Triangle fill | O(pixels × overdraw) | the dominant cost at 640×480+; currently single-threaded |
| Texture sampling | O(covered pixels) | inner loop of the fill |
| Clear | O(pixels) | 0.3M pixels at 640×480 |
| SDL upload | O(pixels) | one `SDL_UpdateTexture` per frame |

`bench/frame_bench.cpp` (to add) should confirm this per asset and resolution
before anything below is optimized — the ordering is an expectation, not a
measurement.

## The CPU plan (ordered by expected win)

### 1. Tile-parallel rasterization

The fill loop is the one stage that is still sequential. Split the framebuffer
into horizontal bands (or 32×32 tiles) and rasterize each band on its own
thread with `fp::par_for` over the band list:

- one binning pass computes each triangle's band range (a cheap min/max on the
  already-sorted screen-space vertices);
- each band walks the painter-sorted triangle list, filling only the triangles
  that intersect it;
- bands are disjoint, so no synchronization is needed, and the painter order is
  preserved *within* each band, which keeps the algorithm correct.

This is the standard software-renderer structure and it fits fp directly
(`fp::par_for`, `fp::views::chunk`). Expect the biggest single win on
`bunny.obj` and at higher resolutions; on a 12-face cube it is pointless.

### 2. Cull before you transform

Backface culling (signed area of the projected triangle, or a dot product in
view space) removes roughly half the fill work for closed meshes; frustum
culling skips triangles entirely outside the view. Both are cheap, allocation-
free predicates that can run as `fp::filter`/`filter_map` stages before the
fill — and they compose with the `par_map` transform pass that already exists.

### 3. SIMD where it fits

- The lighting/intensity pass already uses `fp::clamp_inplace`; keep that.
- The fill's inner loop is data-dependent (per-pixel coverage, texture fetch),
  so SIMD helps less than in the transform; measure before rewriting.
- Texture sampling with `fp::gather` is worth a look for affine spans.
- The transform/projection pass is a good `fp::vec<float>` candidate
  (`fp::map_inplace` over a vertex buffer, 8 lanes at a time).

### 4. Depth buffer instead of the painter's sort (experiment)

A z-buffer removes the per-frame sort and fixes interpenetration, at the cost
of a per-pixel depth compare. At 69k faces the sort is real work; at 2k faces
it is not. This is a measured experiment, not a default change — and it must
keep the rasterizer diff-clean against the original filler (the migration's
20,000-triangle comparison is the regression harness).

### 5. Allocation and flags hygiene

- The re-audit already removed the per-frame allocations
  (`sort_by_inplace`, allocation-free spans, `fp::views::iota`); keep it that
  way — one `std::vector` per frame in a hot loop is a measurable cost.
- Build with `-O2 -march=native` and link `-pthread` (fp::simd requires it).
- Reuse the color buffer and the triangle list across frames; never resize per
  frame.

## Measurement gates

`bench/frame_bench.cpp` (new): per-stage timings (transform, cull, sort, fill,
clear, upload) at 640×480, 1280×720 and 1920×1080, on `cube.obj`, `f22.obj`
and `bunny.obj`. Every change above lands only if the benchmark shows a win on
the asset and resolution it targets.

## What would change the decision

Revisit the GPU only if one of these becomes true:

1. **The target resolution moves to 1080p+ *and* the framebuffer stage shows up
   in profiles** — the only low-risk candidate (a single `transform_inplace`
   clear/post pass, one pinned read-back for the SDL upload).
2. **The renderer becomes device-resident end to end** — a compute rasterizer
   (triangle binning + atomic depth test) is a different renderer, not a port,
   and needs one new ForgeFP primitive (a depth-test/atomic-min kernel).
3. **The asset sizes grow past ~100k faces with high overdraw**, where the fill
   and sort both dominate.

Until then, the software renderer with the CPU plan above is the right target:
it keeps the "compiles and runs everywhere" property, and it is the reference
the fp GPU tier would have to beat.
