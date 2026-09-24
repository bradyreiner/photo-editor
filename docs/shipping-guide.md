# Shipping Guide

This document explains how to ship the current `Looksy` codebase as a downloadable desktop app.

The authoritative public-launch contract is now
[production-launch-checklist.md](production-launch-checklist.md). This guide
remains the detailed packaging reference.

It is written for the repository in its current state:

- native desktop app built with Qt/QML and CMake
- Rust helper binaries for catalog/database operations
- bundled managed PostgreSQL catalog for end-user installs
- macOS packaging via app bundle + DMG path
- Windows packaging via staged install root + MSIX path

This guide covers what is implemented, what must be configured, and what still requires real release infrastructure.

## What "shipping" means in this repo

A real release currently has these parts:

1. Build the desktop app.
2. Build the Rust helper binaries.
3. Bundle the helper binaries beside the app.
4. Package the app per platform.
5. Sign the release artifacts.
6. Publish the artifacts and optional update metadata.

## Current release outputs

### macOS

The repo is set up to produce:

- `Looksy.app`
- packaged artifacts through `cpack`
- bundled PostgreSQL runtime under `Contents/Resources/postgres`

Rust helper binaries are installed beside the app binary under:

- `Looksy.app/Contents/MacOS`

### Windows

The repo is set up to produce:

- a staged install root from `cmake --install`
- an `.msix` package from that staged install root
- an optional `.appinstaller` file for update distribution
- bundled PostgreSQL runtime in the install root

Rust helper binaries are installed beside the app executable in the install root.

## Required local/runtime behavior

The app supports two PostgreSQL modes:

### External PostgreSQL authority

If `PHOTO_EDITOR_DATABASE_URL` is set:

- all ordinary desktop helpers use that PostgreSQL instance
- SQLx migrations run before the catalog repository is exposed
- the managed local service is not started

### Managed local PostgreSQL mode

If `PHOTO_EDITOR_DATABASE_URL` is not set:

- the app initializes a private PostgreSQL cluster under `PHOTO_EDITOR_APP_DATA_DIR/postgres`
- it binds only to loopback using generated local credentials and a private port
- the package supplies `postgres`, `initdb`, `pg_ctl`, `pg_dump`, `pg_restore`, and `psql`
- SQLx migrations and health checks run before use

This managed mode is what downloadable releases are built around.

## Repo configuration you need before shipping

### GitHub secrets

Set these in the repository before making signed releases:

- `APPLE_TEAM_ID`
- `APPLE_DEVELOPER_ID_P12_BASE64`
- `APPLE_DEVELOPER_ID_P12_PASSWORD`
- `APPLE_DEVELOPER_ID_APP`
- `APPLE_NOTARY_APPLE_ID`
- `APPLE_NOTARY_TEAM_ID`
- `APPLE_NOTARY_PASSWORD`
- `SPARKLE_ED_PRIVATE_KEY_BASE64`
- `WINDOWS_SIGNING_CERT_BASE64`
- `WINDOWS_SIGNING_CERT_PASSWORD`
- `PUBLIC_RELEASES_TOKEN`
- `CLOUDFLARE_API_TOKEN`
- `CLOUDFLARE_ACCOUNT_ID`

### GitHub variables

Required public release, website, and package metadata are listed in
[production-launch-checklist.md](production-launch-checklist.md). In particular,
the Windows publisher must exactly match the production certificate subject.

Windows package metadata:

- `PHOTO_EDITOR_WINDOWS_PACKAGE_NAME`
- `PHOTO_EDITOR_WINDOWS_PACKAGE_DISPLAY_NAME`
- `PHOTO_EDITOR_WINDOWS_PACKAGE_PUBLISHER`
- `PHOTO_EDITOR_WINDOWS_PUBLISHER_DISPLAY_NAME`
  - Set this to the approved `LOOKSY_LEGAL_PUBLISHER`, not the Looksy product name unless they are legally identical.
- `PHOTO_EDITOR_WINDOWS_DESCRIPTION`
- `PHOTO_EDITOR_WINDOWS_TIMESTAMP_URL`
- `PHOTO_EDITOR_WINDOWS_APPINSTALLER_URI`
- `PHOTO_EDITOR_WINDOWS_PACKAGE_BASE_URI`

macOS updater metadata:

- `PHOTO_EDITOR_MACOS_SPARKLE_ARCHIVE`
- `PHOTO_EDITOR_MACOS_SPARKLE_URL`
- `PHOTO_EDITOR_MACOS_APPCAST_URL`
- `PHOTO_EDITOR_MACOS_SPARKLE_PUBLIC_KEY`

## Versioning rules

Use the root `VERSION` file as the source of truth, and Git tags in the form:

```text
vX.Y.Z
```

Examples:

```text
v0.1.0
v0.2.3
v1.0.0
```

The release workflow validates that the tag matches `VERSION`.

For database compatibility:

- schema version comes from `rust/crates/catalog/migrations`
- app version comes from the root `VERSION` file and matching Git tag
- edit recipe versioning is still application-owned and should be treated separately

## How to build a release locally

### Repeatable local macOS release script

For the normal local macOS release build, use:

```sh
./scripts/build-release.sh 0.1.0
```

For the next release, pass the next version:

```sh
./scripts/build-release.sh 0.1.1
```

The script updates `VERSION`, runs the Rust and native test gates, builds release Rust helpers, configures the release CMake preset, packages with CPack, and writes `SHA256SUMS.txt` beside the package artifacts.

By default, the script requires a clean worktree. For a local candidate build from uncommitted changes, use:

```sh
./scripts/build-release.sh 0.1.0 --allow-dirty
```

For an official tagged release, commit the `VERSION` bump first, then run:

```sh
./scripts/build-release.sh 0.1.0 --create-tag
```

Before sending artifacts to users, sign and notarize the macOS output, test it on a clean machine, and publish the artifact with release notes and checksums.

### macOS

Prerequisites:

- Qt installed
- CMake and Ninja installed
- Rust toolchain installed

Build:

```sh
cargo build --manifest-path rust/Cargo.toml -p photo-catalog --bins --release
cmake --preset release-macos -DPHOTO_EDITOR_RUST_BINARY_DIR="$PWD/rust/target/release"
cmake --build --preset build-release-macos
```

Package:

```sh
cpack --config build/release-macos/CPackConfig.cmake -B build/release-macos/package
```

If you want to enable the Sparkle updater in the macOS app bundle:

```sh
cmake --preset release-macos \
  -DPHOTO_EDITOR_RUST_BINARY_DIR="$PWD/rust/target/release" \
  -DPHOTO_EDITOR_ENABLE_MACOS_SPARKLE_UPDATER=ON \
  -DPHOTO_EDITOR_SPARKLE_DISTRIBUTION_DIR="/path/to/Sparkle-distribution" \
  -DPHOTO_EDITOR_MACOS_APPCAST_URL="https://example.com/appcast.xml" \
  -DPHOTO_EDITOR_MACOS_SPARKLE_PUBLIC_KEY="<sparkle-public-ed-key>"
```

### Windows

Prerequisites:

- Windows runner or machine with Visual Studio build tools
- Qt installed
- Windows SDK installed
- Rust toolchain installed

Build:

```powershell
cargo build --manifest-path rust/Cargo.toml -p photo-catalog --bins --release
cmake --preset release-windows -DPHOTO_EDITOR_RUST_BINARY_DIR="$PWD\rust\target\release"
cmake --build --preset build-release-windows
cmake --install build/release-windows --prefix "$env:TEMP\photo-editor-windows-install"
```

Create the MSIX package:

```powershell
./scripts/package-windows-msix.ps1 `
  -InstallRoot "$env:TEMP\photo-editor-windows-install" `
  -OutputDir "build/release-windows/package" `
  -Version "0.1.0"
```

## How the GitHub Actions release works

The release workflow is in:

- `.github/workflows/release.yml`

It does this:

1. Checks out the repo.
2. Installs the Rust toolchain.
3. Installs Qt.
4. Builds all Rust helper binaries in release mode.
5. Optionally prepares a Sparkle distribution for macOS update support.
6. Configures CMake with:
   - `PHOTO_EDITOR_RUST_BINARY_DIR`
   - optionally Sparkle updater settings for macOS
7. Builds the desktop app.
8. Packages:
   - macOS via `cpack`
   - Windows via `package-windows-msix.ps1`
9. Optionally generates a Sparkle appcast from the packaged macOS artifacts.
10. Uploads the packaged artifacts.

## How to ship the first release

### Step 1: prepare metadata and credentials

- create your Apple Developer ID assets
- create/export your Windows signing certificate
- configure the GitHub secrets and variables listed above

### Step 2: create a release tag

```sh
git tag v0.1.0
git push origin v0.1.0
```

### Step 4: inspect the workflow artifacts

Check that:

- macOS artifact contains `Looksy.app`
- Rust helpers are bundled beside the app binary
- Windows artifact contains an `.msix`
- Windows artifact contains an `.appinstaller` if update URIs were configured

### Step 5: validate on clean machines

Before public release, test on a clean macOS machine and a clean Windows machine:

- install the app
- confirm startup works without dev tools
- confirm the managed local database bootstraps
- import sample media
- save edits
- relaunch and verify persisted catalog data

## Recommended publish flow

### macOS

Recommended final publish flow:

1. build the release
2. sign the app bundle
3. if Sparkle is enabled, generate and publish the appcast feed
4. notarize the bundle or DMG
5. staple the notarization result
6. upload the final artifact and appcast

### Windows

Recommended final publish flow:

1. stage the install root
2. create the `.msix`
3. sign the package
4. optionally publish the `.appinstaller`
5. upload the final artifact

## Update distribution

### macOS

The repo now supports an optional Sparkle-based update path on macOS. For real update delivery, host:

- the signed app artifact
- the update feed

For Sparkle to work in production:

- the app bundle must be built with `PHOTO_EDITOR_ENABLE_MACOS_SPARKLE_UPDATER=ON`
- the bundle must include `Sparkle.framework`
- the app must contain `SUFeedURL` and `SUPublicEDKey`
- the published appcast must point to the hosted archive URL

### Windows

The Windows path is set up for App Installer style updates. To use that:

- set `PHOTO_EDITOR_WINDOWS_APPINSTALLER_URI`
- set `PHOTO_EDITOR_WINDOWS_PACKAGE_BASE_URI`
- publish both the `.msix` and `.appinstaller` to those URLs

## What is implemented vs not implemented

### Implemented

- PostgreSQL SQLx migrations, managed startup/health, and pre-migration `pg_dump` backup
- helper binary installation beside the packaged app
- macOS package path through CPack
- optional Sparkle updater integration for macOS bundles
- macOS appcast generation hook for release packaging
- Windows MSIX generation script
- Windows App Installer metadata generation

### Implemented but awaiting real credentials and clean-machine proof

- Apple signing/notarization automation inside the protected release workflow
- Windows signing validation using the production certificate
- production-quality macOS and Windows package art
- signed Sparkle appcast generation and App Installer publishing
- public binary-repository promotion and Cloudflare Pages deployment

The workflow intentionally refuses a public release until the real certificate,
publisher, updater, website, support, payment, and hosting values are configured.

## Minimum release checklist

- all tests pass
- Rust helpers build in release mode
- release workflow variables are configured
- release workflow secrets are configured
- artifacts build on GitHub Actions
- macOS install tested on a clean machine
- Windows install tested on a clean machine
- managed PostgreSQL catalog opens, persists, backs up, and restores successfully
- import/edit/relaunch flow works

## Troubleshooting

### Windows MSIX packaging fails

Check:

- Windows SDK is installed
- `makeappx.exe` is available
- `signtool.exe` is available if signing is enabled
- the install root actually contains `bin\Looksy.exe`

### Local catalog does not open in a packaged build

Check:

- Rust helper binaries are bundled beside the app
- the app has permission to create files under its app data directory
- `catalog_local_db status` reports `mode: "local-managed"`, `healthy: true`, and
  `initialized: true`
- `catalog_snapshot` succeeds against the same managed connection after all checked SQLx
  migrations have applied

### Release artifact builds but is not installable

Check:

- signing certificate publisher matches the MSIX manifest publisher
- package version is valid and monotonic
- the clean test machine has App Installer support

## Related docs

- [release-process.md](/Users/bradyreiner/projects/PersonalProjects/photo-editor/docs/release-process.md)
- [local-catalog-storage.md](/Users/bradyreiner/projects/PersonalProjects/photo-editor/docs/local-catalog-storage.md)
- [database-migrations.md](/Users/bradyreiner/projects/PersonalProjects/photo-editor/docs/database-migrations.md)
