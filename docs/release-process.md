# Looksy Release Process

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

- `LOOKSY_PUBLIC_RELEASE_REPOSITORY`
- `LOOKSY_WEBSITE_URL`
- `LOOKSY_SUPPORT_EMAIL`
- `LOOKSY_LEGAL_PUBLISHER`
- `LOOKSY_SUPPORT_PAYMENT_URL`
- `LOOKSY_PURCHASE_URL` (optional for the free alpha)
- `LOOKSY_CLOUDFLARE_PAGES_PROJECT`
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

## Qt account and distribution licensing

A Qt Account prompt depends on how Qt is acquired; it does not determine how
Looksy may be distributed. A macOS development install obtained through
Homebrew may not request a Qt Account because Homebrew is the package source.
The official Qt Online Installer on Windows requests a Qt Account before it
downloads prebuilt Qt packages. Qt describes that login as part of its
[binary-download policy](https://www.qt.io/blog/qt-offering-changes-2020); it is
not a purchase of, or substitute for, a commercial Qt license.

The same release-license decision applies on macOS and Windows, regardless of
whether installation required an account. Before shipping either platform, the
release owner must record and verify one of these distribution models:

- a commercial Qt license that covers the developers, Qt version, modules,
  build process, and distribution channels used by the release; or
- an LGPL-compliant release for every shipped Qt module and its applicable
  third-party dependencies.

For an LGPL release, retain evidence that the package provides all required
license texts and notices, corresponding source or a valid publisher-controlled
source offer, and practical instructions and means for recipients to replace or
relink the dynamically linked Qt libraries. The product terms and packaging
must not prohibit the reverse engineering needed to exercise those LGPL rights.
Publish source for any LGPL-covered Qt modifications and separately audit Qt
Multimedia codecs and other bundled third-party components. Review MSIX, store,
signing, and update restrictions specifically; do not ship an LGPL build through
a channel that prevents the recipient from exercising the required rights.

Use Qt's current [open-source obligations](https://www.qt.io/development/open-source-lgpl-obligations)
and [open-source licensing FAQ](https://www.qt.io/faq/qt-open-source-licensing)
as the baseline, and obtain legal review for the final package and terms. An
installer account—or the absence of one—is not evidence that this gate passed.

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
