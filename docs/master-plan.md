# Photo Editor Master Plan

## Goal

Deliver a native professional photo editor that competes on:

- image quality
- color precision
- masking depth
- large-project throughput
- dependable catalog and export workflows

## Delivery sequence

### Foundation

- finalize benchmarks and product success criteria
- stabilize native build, Docker, Compose, and developer workflow
- establish PostgreSQL-backed catalog service boundaries

## Immediate product focus

The highest-leverage near-term path is not broad feature coverage across every workspace.

It is one complete professional photo workflow:

`import -> cull -> batch edit -> export`

That means:

- prioritize library speed and culling depth before adding adjacent editing tools
- treat batch operations as a core product feature, not a later convenience
- turn masking into a real stack-based workflow rather than adding isolated local tools
- harden catalog recovery and export reliability as product features

For the prioritized breakdown, see [docs/next-level-roadmap.md](/Users/bradyreiner/projects/PersonalProjects/photo-editor/docs/next-level-roadmap.md).
For the concrete color-management and render-pipeline plan, see [docs/color-science-roadmap.md](/Users/bradyreiner/projects/PersonalProjects/photo-editor/docs/color-science-roadmap.md).

### Core workflow

- ingest files from multiple drives
- extract metadata
- browse by date, camera, and collection
- rate, cull, compare, and filter

### Editing

- render previews from real files
- apply nondestructive edit recipes
- implement high-value manual controls first
- add masking and local adjustment depth

### Delivery

- sync edits and presets across selections
- export large batches reliably
- report progress and failures clearly

### Product hardening

- cross-platform packaging
- diagnostics and crash handling
- performance tuning
- beta validation with real photographer workloads

## Scope discipline

The repository can continue to host carousel and timeline experiments, but they should not outrank the core photo workflow unless the product direction explicitly changes.

## Non-negotiables

- native desktop UX
- pro-grade color workflow
- managed and referenced libraries
- multi-drive awareness
- fast culling and dependable exports
- distinctive branded interface
