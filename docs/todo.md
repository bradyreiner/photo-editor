# Master Implementation Plan

## Phase 1: Product and technical foundations

1. Finalize competitor research across Lightroom Classic, Capture One, DxO, ON1, and Photo Mechanic.
2. Define image-quality benchmarks for portraits, weddings, mixed lighting, landscapes, and high ISO.
3. Lock v1 user workflows: import, cull, edit, sync, export, relink offline drives.
4. Define render-latency and batch-throughput targets.
5. Keep the repo scaffold aligned with the architecture as implementation advances.

## Phase 2: Catalog and persistence

6. Wire the Rust catalog crate to a real PostgreSQL connection pool.
7. Add schema bootstrap and migrations.
8. Implement drive records and asset records.
9. Implement referenced and managed import modes.
10. Implement date and camera metadata indexing.
11. Implement relink and offline-drive status handling.
12. Add catalog CRUD and filtered query APIs.
13. Add export job persistence.
14. Add versioning strategy for catalog schema and edit recipes.

## Phase 3: Import and metadata ingestion

15. Add file discovery and recursive folder ingest.
16. Add EXIF/XMP parsing.
17. Extract year, month, day, camera make/model, lens, ISO, shutter, and aperture.
18. Persist import decisions and source locations.
19. Add managed-library path templates.
20. Add duplicate detection strategy.

## Phase 4: Library workflow

21. Replace mock Library panels with a real asset browser.
22. Add grid, loupe, and compare surfaces.
23. Add ratings, rejects, flags, and color labels.
24. Add filtering by date, camera, lens, label, and drive state.
25. Add collections and smart collections.
26. Add multi-drive visibility and missing-file recovery UX.

## Phase 5: Preview and render pipeline

27. Add preview generation service and cache strategy.
28. Add render requests and preview pyramid handling.
29. Add the first working edit recipe evaluator.
30. Add high-bit-depth working buffer design.
31. Add display/output color transform boundaries.

## Phase 6: Core manual editing

32. Implement exposure, contrast, highlights, shadows, whites, and blacks.
33. Implement temperature, tint, and eyedropper white balance.
34. Implement clarity, texture, dehaze, vibrance, and saturation.
35. Implement tone curve and color mixer controls.
36. Implement crop, straighten, rotate, and transform.
37. Implement sharpening and noise reduction.
38. Implement lens corrections and chromatic aberration correction.
39. Implement healing and clone basics.

## Phase 7: Masking and local adjustments

40. Implement brush, linear, and radial masks.
41. Implement luminance and color range masks.
42. Implement add, subtract, intersect, and feather workflows.
43. Implement per-mask local adjustments.
44. Add AI-assisted subject, sky, and object masks.

## Phase 8: Batch and delivery

45. Implement copy/paste settings.
46. Implement selective sync across many images.
47. Implement preset save/load and partial preset application.
48. Implement export queue execution and failure recovery.
49. Implement export presets for proof, web, and full-resolution delivery.

## Phase 9: Performance and platform acceleration

50. Add background job prioritization.
51. Add GPU backend strategy for Metal.
52. Add GPU backend strategy for Windows.
53. Add render regression suite and performance benchmarks.
54. Optimize large-job browsing and export throughput.

## Phase 10: Productization

55. Add native macOS and Windows CI.
56. Add packaging, signing, and notarization.
57. Add logging, diagnostics, and crash reporting.
58. Add preferences, cache management, and recovery tooling.
59. Add licensing/update-channel groundwork.

## Current implementation focus

The active build focus is:

1. PostgreSQL catalog service
2. import and metadata ingestion
3. real Library workspace data
4. first preview-enabled edit workflow
