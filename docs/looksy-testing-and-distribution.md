# Looksy Testing and Distribution

The first paid release targets **Apple silicon Macs**, with a one-time license
for the purchased major version through Lemon Squeezy. See
[commercial-launch.md](commercial-launch.md). Intel and Windows items below
apply when those platforms are added to `release/product.json`; they are deferred
for the first paid release.


This guide separates a local developer run, a private candidate download, and a
public release. A successful compile or an Actions artifact does not prove a
clean-machine install.

## Local development run

Use the repository launcher on a configured macOS development machine:

```sh
./scripts/run-native.sh
```

The launcher builds the desktop app and Rust helpers, prepares its runtime,
and opens Looksy. It requires the native dependencies described in
[shipping-guide.md](shipping-guide.md). This path is for development; it is
not an installer for external testers.

## Private candidate build

The release workflow keeps packaged artifacts in GitHub Actions for 14 days.
For a branch build, use **Actions → Release → Run workflow** and set
`publish` to `false`. Branch builds are unsigned and are useful for internal
engineering checks only.

For a signed candidate, create and push a version tag only after the package
credentials are configured. The tag run signs and notarizes its packages, then
waits at the protected `production` environment before public promotion. Testers
can download the package artifacts from the workflow run while promotion is
pending. Do not approve `production` until the candidate has passed clean-machine
testing. A manual run against a tag with `publish=false` also skips appcast,
public release, and website deployment.

Before sending a candidate, provide the matching platform package, version,
known limitations, installation steps, and a private channel for feedback. Keep
the release tag and package identity stable while testers install updates.

## Public downloads

The tagged workflow publishes immutable installers to a public binary-only
GitHub repository and deploys `site/` to Cloudflare Pages after the protected
environment approves promotion. Configure the GitHub Actions values before
releasing:

- `LOOKSY_PUBLIC_RELEASE_REPOSITORY`, `LOOKSY_WEBSITE_URL`,
  `LOOKSY_LEGAL_PUBLISHER`, and `LOOKSY_SUPPORT_EMAIL`
- `LOOKSY_CLOUDFLARE_PAGES_PROJECT` and `LOOKSY_SUPPORT_PAYMENT_URL`
- Apple Developer ID signing and notarization secrets
- Windows signing certificate secrets and the matching package publisher
- Sparkle distribution, appcast URL, and public key
- Cloudflare Pages credentials and a least-privilege public release token

The complete secret and package-variable inventory is in
[production-launch-checklist.md](production-launch-checklist.md).

## Required proof before inviting external testers

- Build macOS arm64, macOS Intel, and Windows x64 packages from the same release
  commit.
- Confirm Apple signatures, notarization, Gatekeeper assessment, and the
  Windows Authenticode signature.
- Install each package on a clean machine without developer tools installed.
- Run the catalog lifecycle and a representative import, edit, mask, export,
  quit, and relaunch workflow.
- Verify upgrades, catalog backups, support and privacy links, checksums, and
  uninstall data retention.
- Resolve all legal, publisher, domain, support, and third-party redistribution
  entries in the production checklist.

## Identity that remains stable

Visible product names, site copy, app bundles, executable names, and installer
display names use Looksy. The current macOS bundle identifier (`com.virel.studio`)
and Windows package identity (`Virel.Studio` / `VirelStudio`) remain compatibility
identifiers for prior private-alpha installs and data. Confirm whether any
external testers need upgrades from those builds before changing these IDs; the
first public release locks them for future updates.

## Current repository evidence

- Looksy app and website assets and release metadata are present.
- `scripts/verify-brand-surface.py` passes against the current source tree.
- No clean-machine install, signed candidate, public account configuration, or
  full runtime validation is established by this source inspection.
- `scripts/generate-app-icons.py` requires Pillow to regenerate all icon sizes;
  the committed Looksy master and platform icon files are already present.
