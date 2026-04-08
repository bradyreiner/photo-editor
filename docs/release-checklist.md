# Release Checklist

This is the practical checklist for shipping a user-facing update.

## Version

- Update the root [`VERSION`](/Users/bradyreiner/projects/PersonalProjects/photo-editor/VERSION) file.
- Keep the version in `X.Y.Z` form.
- Use the same version for release notes and support announcements.
- Tag releases as `vX.Y.Z`.

The release workflow now validates that the Git tag matches `VERSION`.

## Development Build

- Use `cmake --preset dev-macos-debug` and `cmake --build --preset build-dev-macos-debug` for local macOS dev builds.
- Or use [`scripts/run-native.sh`](/Users/bradyreiner/projects/PersonalProjects/photo-editor/scripts/run-native.sh), which now uses the same dev preset.
- Build Rust helpers from `rust/target/debug` for local development.

## Production Build

- macOS: `cmake --preset release-macos -DPHOTO_EDITOR_RUST_BINARY_DIR="$PWD/rust/target/release"`
- macOS package: `cpack --config build/release-macos/CPackConfig.cmake -B build/release-macos/package`
- Windows: `cmake --preset release-windows -DPHOTO_EDITOR_RUST_BINARY_DIR="$PWD\\rust\\target\\release"`
- Windows package: stage install root, then generate `.msix` and optional `.appinstaller`

## Before Tagging

- `cargo test --workspace`
- Relevant native tests pass
- Clean-machine smoke test for install, launch, import, edit, export, relaunch
- Verify Rust helpers are bundled
- Verify managed local Postgres bootstraps in packaged builds
- Verify version shown in-app matches `VERSION`

## How Users Should Upgrade

### macOS

- First install: send users the signed and notarized DMG/app bundle.
- Ongoing updates: use Sparkle.
- Build with `PHOTO_EDITOR_ENABLE_MACOS_SPARKLE_UPDATER=ON`.
- Publish the Sparkle appcast and hosted archive URLs.
- Users should upgrade through the in-app updater or Sparkle prompts, not by manually replacing app contents.

### Windows

- First install and ongoing updates: prefer the `.appinstaller` URL, not a raw `.msix`.
- Publish both the `.msix` and `.appinstaller`.
- App Installer handles upgrade checks on launch.

## Release Notes

- Publish concise user-facing notes for every `vX.Y.Z`.
- Include upgrade notes when schema or compatibility behavior changes.
- Call out any migration, cache reset, or catalog backup recommendations.
