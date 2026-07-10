# Shipping Guide

This document explains how to ship the current `Virel Studio` codebase as a downloadable desktop app.

The authoritative public-launch contract is now
[production-launch-checklist.md](production-launch-checklist.md). This guide
remains the detailed packaging reference.

It is written for the repository in its current state:

- native desktop app built with Qt/QML and CMake
- Rust helper binaries for catalog/database operations
- managed local PostgreSQL support for end-user installs
- macOS packaging via app bundle + DMG path
- Windows packaging via staged install root + MSIX path

This guide covers what is implemented, what must be configured, and what still requires real release infrastructure.

## What "shipping" means in this repo

A real release currently has these parts:

1. Build the desktop app.
2. Build the Rust helper binaries.
3. Bundle the helper binaries beside the app.
4. Bundle a PostgreSQL runtime for the managed local catalog flow.
5. Package the app per platform.
6. Sign the release artifacts.
7. Publish the artifacts and optional update metadata.

## Current release outputs

### macOS

The repo is set up to produce:

- `VirelStudio.app`
- packaged artifacts through `cpack`
- optional bundled PostgreSQL runtime under:
  - `VirelStudio.app/Contents/Resources/postgres`

Rust helper binaries are installed beside the app binary under:

- `VirelStudio.app/Contents/MacOS`

### Windows

The repo is set up to produce:

- a staged install root from `cmake --install`
- an `.msix` package from that staged install root
- an optional `.appinstaller` file for update distribution
- optional bundled PostgreSQL runtime under:
  - `bin/postgres`

Rust helper binaries are installed beside the app executable in the install root.

## Required local/runtime behavior

The app supports two database modes:

### External database mode

If `PHOTO_EDITOR_DATABASE_URL` is set:

- the app uses that PostgreSQL instance
- the app only applies schema migrations
- no local bundled database bootstrap is required

### Managed local database mode

If `PHOTO_EDITOR_DATABASE_URL` is not set:

- the app creates and manages a private local PostgreSQL cluster
- generated cluster state lives under `PHOTO_EDITOR_APP_DATA_DIR`
- the helper binaries start the local database and apply migrations automatically

This managed mode is what downloadable releases are currently built around.

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

Set these for the Windows packaging path:

- `PHOTO_EDITOR_WINDOWS_POSTGRES_RUNTIME_ARCHIVE`
  or
- `PHOTO_EDITOR_WINDOWS_POSTGRES_RUNTIME_URL`

Required public release, website, and package metadata are listed in
[production-launch-checklist.md](production-launch-checklist.md). In particular,
the Windows publisher must exactly match the production certificate subject.

Windows package metadata:

- `PHOTO_EDITOR_WINDOWS_PACKAGE_NAME`
- `PHOTO_EDITOR_WINDOWS_PACKAGE_DISPLAY_NAME`
- `PHOTO_EDITOR_WINDOWS_PACKAGE_PUBLISHER`
- `PHOTO_EDITOR_WINDOWS_PUBLISHER_DISPLAY_NAME`
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
- PostgreSQL runtime available if you want to test the managed local DB path

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

If you want to bundle a local PostgreSQL runtime in the package:

```sh
cmake --preset release-macos \
  -DPHOTO_EDITOR_RUST_BINARY_DIR="$PWD/rust/target/release" \
  -DPHOTO_EDITOR_POSTGRES_RUNTIME_DIR="$(brew --prefix postgresql@16)"
```

If you want to enable the Sparkle updater in the macOS app bundle:

```sh
cmake --preset release-macos \
  -DPHOTO_EDITOR_RUST_BINARY_DIR="$PWD/rust/target/release" \
  -DPHOTO_EDITOR_POSTGRES_RUNTIME_DIR="$(brew --prefix postgresql@16)" \
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
- a PostgreSQL runtime archive or URL configured

Build:

```powershell
cargo build --manifest-path rust/Cargo.toml -p photo-catalog --bins --release
cmake --preset release-windows -DPHOTO_EDITOR_RUST_BINARY_DIR="$PWD\rust\target\release"
cmake --build --preset build-release-windows
cmake --install build/release-windows --prefix "$env:TEMP\photo-editor-windows-install"
```

Prepare a PostgreSQL runtime root if needed:

```powershell
$env:PHOTO_EDITOR_POSTGRES_RUNTIME_ARCHIVE = "C:\path\to\postgres-runtime.zip"
bash ./scripts/prepare-postgres-runtime.sh
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
5. Prepares the PostgreSQL runtime:
   - macOS from `brew --prefix postgresql@16`
   - Windows from an archive path or URL
6. Optionally prepares a Sparkle distribution for macOS update support.
7. Configures CMake with:
   - `PHOTO_EDITOR_RUST_BINARY_DIR`
   - optionally `PHOTO_EDITOR_POSTGRES_RUNTIME_DIR`
   - optionally Sparkle updater settings for macOS
8. Builds the desktop app.
9. Packages:
   - macOS via `cpack`
   - Windows via `package-windows-msix.ps1`
10. Optionally generates a Sparkle appcast from the packaged macOS artifacts.
11. Uploads the packaged artifacts.

## How to ship the first release

### Step 1: prepare metadata and credentials

- create your Apple Developer ID assets
- create/export your Windows signing certificate
- configure the GitHub secrets and variables listed above

### Step 2: prepare the Windows PostgreSQL runtime source

Pick one:

- set `PHOTO_EDITOR_WINDOWS_POSTGRES_RUNTIME_ARCHIVE` to a runner-visible archive path
- set `PHOTO_EDITOR_WINDOWS_POSTGRES_RUNTIME_URL` to a downloadable archive URL

The archive must contain a runtime root with:

- `bin/initdb`
- `bin/pg_ctl`

or Windows equivalents:

- `bin/initdb.exe`
- `bin/pg_ctl.exe`

### Step 3: create a release tag

```sh
git tag v0.1.0
git push origin v0.1.0
```

### Step 4: inspect the workflow artifacts

Check that:

- macOS artifact contains `VirelStudio.app`
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

- Git-tracked database migrations
- managed local PostgreSQL bootstrap
- helper binary installation beside the packaged app
- optional PostgreSQL runtime bundling
- macOS package path through CPack
- optional Sparkle updater integration for macOS bundles
- macOS appcast generation hook for release packaging
- Windows MSIX generation script
- Windows App Installer metadata generation

### Implemented but awaiting real credentials and clean-machine proof

- Apple signing/notarization automation inside the protected release workflow
- Windows signing validation using the production certificate
- production-quality macOS and Windows package art
- Keychain/Credential Manager storage for generated database credentials
- signed Sparkle appcast generation and App Installer publishing
- public binary-repository promotion and Cloudflare Pages deployment

The workflow intentionally refuses a public release until the real certificate,
publisher, updater, website, support, payment, and hosting values are configured.

## Minimum release checklist

- all tests pass
- Rust helpers build in release mode
- release workflow variables are configured
- release workflow secrets are configured
- Windows PostgreSQL runtime source is configured
- artifacts build on GitHub Actions
- macOS install tested on a clean machine
- Windows install tested on a clean machine
- managed local database bootstraps successfully
- import/edit/relaunch flow works

## Troubleshooting

### Windows MSIX packaging fails

Check:

- Windows SDK is installed
- `makeappx.exe` is available
- `signtool.exe` is available if signing is enabled
- the install root actually contains `bin\VirelStudio.exe`

### Local database does not start in a packaged build

Check:

- Rust helper binaries are bundled beside the app
- PostgreSQL runtime exists in the packaged location
- the runtime contains `initdb` and `pg_ctl`
- the app has permission to create files under its app data directory

### Release artifact builds but is not installable

Check:

- signing certificate publisher matches the MSIX manifest publisher
- package version is valid and monotonic
- the clean test machine has App Installer support

## Related docs

- [release-process.md](/Users/bradyreiner/projects/PersonalProjects/photo-editor/docs/release-process.md)
- [local-postgres-service.md](/Users/bradyreiner/projects/PersonalProjects/photo-editor/docs/local-postgres-service.md)
- [database-migrations.md](/Users/bradyreiner/projects/PersonalProjects/photo-editor/docs/database-migrations.md)
