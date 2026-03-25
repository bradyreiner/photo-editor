# Productization Notes

## What Docker solves

- consistent onboarding for developers
- repeatable CI for Rust and core native code
- early smoke validation of the architecture
- a stable environment for non-UI tests
- a standard PostgreSQL service for early product-backed development

## What Docker does not solve

- shipping a polished macOS app bundle
- Windows installer creation and code signing
- Apple notarization
- GPU driver validation on target hardware
- display/color accuracy validation on real photographer setups

## Product path

### Engineering foundation

- Keep the core logic deterministic and testable outside the UI.
- Expand smoke tests into render, catalog, and metadata regression suites.
- Add SQLite migrations early.
- Add logging, diagnostics, and crash reporting before alpha.

### Release engineering

- Add native CI for macOS and Windows.
- Produce signed release artifacts per platform.
- Add auto-update infrastructure only after the app format is stable.
- Version catalog schemas and edit recipes from the start.

### Product requirements

- Professional camera support matrix
- export reliability on very large jobs
- recovery flows for offline drives and missing files
- preference management for cache and preview storage
- support and diagnostics workflow for user-reported rendering issues
- a decision on how PostgreSQL is deployed for customers: local bundled service, remote account service, or hybrid offline cache

### Commercial readiness

- licensing and activation plan
- update channel strategy: alpha, beta, stable
- privacy/telemetry policy if analytics are added
- documentation and onboarding for imports, catalogs, drives, and exports

## Recommended near-term path

1. Use Docker for headless CI and consistent dev setup.
2. Keep shipping-oriented builds native per platform.
3. Build the first product slice around import, catalog, browse, and basic edits.
4. Add release engineering only after the core workflow is stable.
