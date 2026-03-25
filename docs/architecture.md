# Architecture

## System layout

The codebase is split into three native layers:

- `apps/desktop`: Qt 6 + QML application shell and branded workspace surfaces
- `engine`: C++ image-engine layer for render graph, color pipeline, masks, and imaging contracts
- `rust`: catalog, metadata, job orchestration, and FFI-facing service crates

## Product constraints

- Native desktop only for v1
- local-first catalog
- referenced and managed imports
- multi-drive aware asset tracking
- nondestructive edit recipes
- batch-oriented professional workflow support

## Core subsystems

### Desktop UI

The desktop shell owns:

- workspace navigation
- culling/browser views
- inspector panels
- masking UI
- export queue UI
- branded design system primitives

### Engine

The engine owns:

- image render graph
- RAW/raster processing stages
- color management
- mask evaluation
- detail stages
- display and export transforms

### Catalog services

Rust services own:

- asset location records
- import mode handling
- metadata indexing
- export job orchestration
- PostgreSQL-backed persistence and future offline caching strategy
- schema bootstrap and repository operations for drives, assets, and export jobs

## Persistence direction

- The product-facing catalog is now scaffolded around PostgreSQL.
- Docker Compose provides a local PostgreSQL service for development.
- For a real desktop shipping product, the next architectural decision is whether PostgreSQL remains a bundled/local service or becomes part of a synced product backend with offline cache on device.
- The current repository implements the first PostgreSQL service boundary in the Rust catalog crate.

## Initial boundaries

- QML should talk to C++/Rust-backed models rather than implement workflow logic itself.
- The render graph should remain deterministic and testable outside the UI.
- Catalog and job entities should be versioned before persistence is introduced.
