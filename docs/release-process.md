# Release Process

## Current foundation

- GitHub Actions CI for headless validation
- GitHub Actions security checks for dependency audit and secret scanning
- GitHub Actions release packaging for macOS and Windows
- app-owned runtime directories for local support files
- Git-tracked catalog migrations
- app-managed local Postgres helper for default desktop startup
- install rules for bundling Rust helpers beside the packaged desktop app
- optional packaging hook for bundling a Postgres runtime root

## Required secrets for real signed releases

- `APPLE_TEAM_ID`
- `APPLE_DEVELOPER_ID_APP`
- `APPLE_DEVELOPER_ID_INSTALLER`
- `APPLE_NOTARY_APPLE_ID`
- `APPLE_NOTARY_TEAM_ID`
- `APPLE_NOTARY_PASSWORD`
- `WINDOWS_SIGNING_CERT_BASE64`
- `WINDOWS_SIGNING_CERT_PASSWORD`

## Recommended repository variables

- `PHOTO_EDITOR_MACOS_SPARKLE_ARCHIVE`
- `PHOTO_EDITOR_MACOS_SPARKLE_URL`
- `PHOTO_EDITOR_MACOS_APPCAST_URL`
- `PHOTO_EDITOR_MACOS_SPARKLE_PUBLIC_KEY`
- `PHOTO_EDITOR_WINDOWS_POSTGRES_RUNTIME_ARCHIVE`
- `PHOTO_EDITOR_WINDOWS_POSTGRES_RUNTIME_URL`
- `PHOTO_EDITOR_WINDOWS_PACKAGE_NAME`
- `PHOTO_EDITOR_WINDOWS_PACKAGE_DISPLAY_NAME`
- `PHOTO_EDITOR_WINDOWS_PACKAGE_PUBLISHER`
- `PHOTO_EDITOR_WINDOWS_PUBLISHER_DISPLAY_NAME`
- `PHOTO_EDITOR_WINDOWS_DESCRIPTION`
- `PHOTO_EDITOR_WINDOWS_TIMESTAMP_URL`
- `PHOTO_EDITOR_WINDOWS_APPINSTALLER_URI`
- `PHOTO_EDITOR_WINDOWS_PACKAGE_BASE_URI`

## Recommended release flow

1. Merge into `main` only through reviewed pull requests.
2. Update the root `VERSION` file to `X.Y.Z`.
3. Tag a release as `vX.Y.Z`.
4. Let GitHub Actions build artifacts for macOS and Windows.
5. Bundle the Rust helper binaries from `rust/target/release`.
6. Bundle a PostgreSQL runtime root where available.
   macOS currently uses `brew --prefix postgresql@16` in CI.
   Windows uses either `PHOTO_EDITOR_WINDOWS_POSTGRES_RUNTIME_ARCHIVE` or `PHOTO_EDITOR_WINDOWS_POSTGRES_RUNTIME_URL`.
7. When Sparkle is configured, bundle `Sparkle.framework` into the macOS app and generate an appcast from the packaged artifacts.
8. Build an `.msix` from the staged Windows install root and generate an `.appinstaller` file when the update URIs are configured.
9. Sign and notarize macOS artifacts.
10. Sign Windows artifacts and publish the installer/update metadata.
11. Publish release notes plus migration compatibility notes.

## Version contracts

- App version: root `VERSION` file, CMake project version, bundle version, and Git tag
- Catalog schema version: `sqlx` migration history
- Edit recipe version: application-controlled JSON contract
- Release channel: alpha, beta, stable
