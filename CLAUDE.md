# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Photo Editor is a desktop photo/video editing application built with three layers:
- **Desktop UI** (`apps/desktop/`) — Qt 6 + QML application with workspaces for Library, Edit, Carousel, Timeline, and Export
- **C++ Engine** (`engine/`) — C++20 image processing pipeline: render graph, color adjustments, crop/mask evaluation, EditRecipe serialization
- **Rust Services** (`rust/`) — PostgreSQL-backed catalog, EXIF metadata extraction, import workflows, export job orchestration, with FFI bridge to C++

## Build Commands

### macOS Development (quickest path)
```bash
./scripts/run-native.sh          # Full build + launch (handles Rust, CMake, Postgres)
```

### Manual CMake Build
```bash
# Configure
cmake --preset dev-macos-debug       # macOS debug
cmake --preset dev-windows-debug     # Windows debug
cmake --preset ci-headless           # Linux CI (no Qt)

# Build
cmake --build --preset build-dev-macos-debug
cmake --build --preset build-ci-headless

# Run all C++ tests
ctest --preset test-ci-headless                           # headless
cd build/dev-macos-debug && ctest --output-on-failure     # desktop

# Run a single C++ test
ctest -R editor_preview_controller_test --output-on-failure
```

### Rust
```bash
cd rust
cargo build -p photo-catalog --bins   # Build catalog binaries
cargo test --workspace                # Run all Rust tests
cargo test -p photo-catalog           # Test single crate
```

### Docker
```bash
docker compose up -d postgres         # Start local Postgres (port 5433)
docker compose run --rm dev           # Headless dev shell with Postgres
```

## CMake Options

| Option | Default | Purpose |
|--------|---------|---------|
| `PHOTO_EDITOR_BUILD_DESKTOP` | ON | Include Qt/QML desktop app |
| `PHOTO_EDITOR_ENABLE_RUST` | ON | Enable Rust workspace integration |
| `PHOTO_EDITOR_BUILD_TESTS` | ON | Build native tests |
| `PHOTO_EDITOR_ENABLE_MACOS_SPARKLE_UPDATER` | OFF | macOS auto-updates via Sparkle |

## Architecture

### Controller Pattern (Desktop)
Qt controllers in `apps/desktop/src/` expose C++ models to QML via context properties. Key controllers:
- `AppBootstrap` — runtime directory setup, catalog bootstrap, process environment
- `AppShellController` — app preferences, workspace navigation
- `EditorPreviewController` — single-image editing with 17+ adjustable Q_PROPERTY fields
- `CarouselDocumentController` — multi-slide composition with linked groups
- `TimelineDocumentController` — video timeline with tracks, clips, transitions, effects
- `ExportJobController` — batch export queue

### Engine Data Model
`EditRecipe` (recipe_version 3) is the central serialization format containing:
- `GlobalAdjustments` — 13 tone/color sliders
- `CurveSetDefinition` — master + per-channel RGB curves
- `ColorMixerDefinition` — HSL adjustments per color channel
- `MaskDefinition` — linear/radial masks with geometry
- `CropTransformDefinition` — crop geometry with aspect presets

### Rust ↔ C++ Bridge
The `photo-ffi` crate (`rust/crates/ffi/`) exports a C-compatible static library consumed by CMake via `cmake/RustInterop.cmake`. Catalog operations flow: QML → Qt controller → engine headers → FFI → Rust catalog crate → PostgreSQL.

### Catalog & Database
PostgreSQL 16 on localhost:5433 (user/pass: `photo_editor`). SQL migrations live in `rust/crates/catalog/migrations/` and are tracked in git. Key binaries:
- `catalog_local_db` — manages embedded Postgres lifecycle (ensure-running / status / stop)
- `catalog_migrate` — applies migrations
- `import_catalog` — imports photos from a directory

### QML UI Structure
`apps/desktop/qml/Main.qml` is the window root with workspace tabs. Reusable components live in `qml/components/`, workspace views in `qml/workspaces/`. Theme system supports light/dark via `Theme.qml`.

## Environment Variables

See `.env.example`. Key ones:
- `PHOTO_EDITOR_DATABASE_URL` — override Postgres connection string
- `PHOTO_EDITOR_POSTGRES_HOST_PORT` — local Postgres port (default 5433)
- `PHOTO_EDITOR_APP_DATA_DIR` — app runtime data root

## CI

GitHub Actions runs on PRs and main pushes (`.github/workflows/ci.yml`):
- `cargo test --workspace` (Rust)
- CMake `ci-headless` preset build + CTest (C++ engine tests, no Qt)

Compiler warnings are strict: `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion` (Clang/GCC) or `/W4 /permissive-` (MSVC), applied via `cmake/CompilerWarnings.cmake`.
