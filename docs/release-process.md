# Virel Studio Release Process

The tagged release workflow is fail closed. It tests, packages, signs, verifies,
promotes immutable public assets, and then deploys the download site. The private
source repository is never used as the anonymous download host.

## Release topology

1. Private source repository receives tag `vX.Y.Z` matching `VERSION`.
2. `quality-gate` runs Rust and headless native tests and exports dependency evidence.
3. Platform jobs build:
   - macOS arm64 on `macos-14`
   - macOS x86_64 on `macos-15-intel`
   - Windows x64 on `windows-2022`
4. macOS jobs sign with Hardened Runtime, notarize, staple, and assess each DMG.
5. Windows signs, timestamps, and validates the MSIX.
6. A macOS job creates one EdDSA-signed appcast from both notarized DMGs.
7. `publish` promotes only verified artifacts into the configured public binary repository.
8. `deploy-site` materializes public configuration and deploys `site/` to Cloudflare Pages.

## Required secrets

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

Store signing and Sparkle private keys as production-environment secrets. Keep
offline backups. `PUBLIC_RELEASES_TOKEN` should have access only to the public
binary repository.

## Required variables

- `VIREL_PUBLIC_RELEASE_REPOSITORY`
- `VIREL_WEBSITE_URL`
- `VIREL_SUPPORT_EMAIL`
- `VIREL_SUPPORT_PAYMENT_URL`
- `VIREL_PURCHASE_URL` (optional for the free alpha)
- `VIREL_CLOUDFLARE_PAGES_PROJECT`
- `PHOTO_EDITOR_MACOS_SPARKLE_ARCHIVE` or `PHOTO_EDITOR_MACOS_SPARKLE_URL`
- `PHOTO_EDITOR_MACOS_APPCAST_URL`
- `PHOTO_EDITOR_MACOS_SPARKLE_PUBLIC_KEY`
- Windows PostgreSQL, package publisher, timestamp, App Installer, and package-base values listed in [production-launch-checklist.md](production-launch-checklist.md)

## Version contracts

- `VERSION`, CMake version, bundle version, MSIX version, release notes, and Git tag must agree.
- macOS and Windows package identities must never change after the first public build.
- Release assets are immutable. Correct a release with a new patch version.
- Catalog schema changes are forward migrations. A versioned database dump is created before migration.
- Alpha is the current product channel, but GitHub marks the release as `latest` so stable `/latest/download/` updater URLs work.

## Public release gate

Run locally for structural validation:

```sh
python3 scripts/release-preflight.py --mode candidate --allow-dirty
python3 scripts/validate-release-site.py site
```

The public CI gate additionally requires the repository variables above and a
clean tag. The protected `production` environment should require manual review.

## Recovery rules

- Never replace a published asset in place after users install it.
- Stop promotion if either architecture, signature, notarization, appcast signature, MSIX signature, checksum, or legal inventory is missing.
- If an update is bad, publish a higher patch version and update the appcast; do not instruct users to downgrade a migrated catalog.
- Preserve the matching pre-migration dump until the user confirms the new version is healthy.
