# Virel Studio Production Launch Checklist

This is the release contract for turning a tagged commit into public,
upgradeable desktop installers. Do not mark an external item complete until the
credential, account, review, or clean-machine proof actually exists.

## Implemented in the repository

- [x] Public product identity is centralized as `Virel Studio`.
- [x] Stable default macOS bundle identifier is `com.virel.studio`.
- [x] macOS and Windows packages use production icon assets.
- [x] macOS packages include an app icon, legal notice, Rust helpers, Qt, and PostgreSQL runtime.
- [x] Windows packages use committed production visual assets rather than generated placeholders.
- [x] Windows `.appinstaller` metadata checks for updates on launch.
- [x] Sparkle is wired with a public EdDSA key and an HTTPS appcast URL.
- [x] Public releases fail if required identity, updater, website, publisher, or payment configuration is absent.
- [x] Tagged builds run Rust tests and the headless native test suite before packaging.
- [x] macOS release jobs import an ephemeral Developer ID certificate, sign with Hardened Runtime, notarize, staple, and verify both architectures.
- [x] Windows release jobs sign, timestamp, and verify the MSIX signature.
- [x] Sparkle appcasts are generated from both notarized DMGs using a private key provided through CI standard input.
- [x] Public release promotion includes checksums, a release manifest, legal notices, and locked Rust dependency inventory.
- [x] Public artifacts are promoted to a separate public GitHub release repository; the private source repository is not exposed.
- [x] The website resolves the latest Apple-silicon, Intel, and Windows assets from the public release repository.
- [x] The website includes support/payment links plus privacy, terms, refund, license, and support pages.
- [x] Website deployment is automated through the existing Cloudflare Pages path after successful release promotion.
- [x] Production builds retain private local diagnostics and create an explicit review-before-sharing support bundle.
- [x] Managed PostgreSQL credentials migrate to Keychain on macOS and Credential Manager on Windows.
- [x] Existing managed catalogs receive a versioned `pg_dump` backup before schema migrations.
- [x] Release artifacts include `pg_dump` and `pg_restore` so backup and recovery do not depend on developer tools.

## External launch blockers

- [ ] Complete trademark/name clearance for `Virel Studio` and approve the permanent legal publisher name.
- [ ] Own and configure the production website domain used by `VIREL_WEBSITE_URL`.
- [ ] Enroll the legal publisher in the Apple Developer Program.
- [ ] Export the Developer ID Application certificate as a protected P12 and configure the CI secrets.
- [ ] Create and protect the Apple notarization app-specific password.
- [ ] Obtain a publicly trusted Windows code-signing certificate and set the package publisher to its exact subject.
- [ ] Create the Sparkle EdDSA keypair, back up the private key offline, and configure public/private CI values.
- [ ] Create a public binary-only GitHub repository and a least-privilege token that can publish releases there.
- [ ] Create the Cloudflare Pages project, production domain, API token, and account variables.
- [ ] Create the hosted payment link. The public alpha defaults to free access plus optional support.
- [ ] Configure the real support email and an operating process for refunds and security reports.
- [ ] Have qualified counsel review the evaluation license, privacy policy, terms, refund policy, Qt distribution model, and tax posture.
- [ ] Confirm every bundled AI model, codec, camera profile, font, image, and dependency is redistributable.

## Required clean-machine proof

- [ ] Install the Apple-silicon DMG on a clean supported Mac.
- [ ] Install the Intel DMG on a clean supported Intel Mac.
- [ ] Install `VirelStudio.appinstaller` on a clean Windows 10 22H2 or Windows 11 x64 machine.
- [ ] Confirm first launch works without Homebrew, Qt, Rust, PostgreSQL, Visual Studio, or other developer tools.
- [ ] Confirm managed database bootstrap, import, cull, edit, mask, export, quit, and relaunch.
- [ ] Confirm uninstall/reinstall preserves user data unless the user explicitly removes it.
- [ ] Publish a private `0.1.0` candidate and prove Sparkle updates it to `0.1.1` on both Mac architectures.
- [ ] Prove App Installer updates `0.1.0.0` to `0.1.1.0` without changing package identity.
- [ ] Confirm the pre-migration dump is created and a documented restore succeeds.
- [ ] Test missing drive, full disk, denied folder permission, interrupted export, corrupted cache, and unavailable update feed.
- [ ] Confirm every website download hash matches the promoted release manifest.

## Required production repository configuration

### Secrets

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

### Variables

- `VIREL_PUBLIC_RELEASE_REPOSITORY`
- `VIREL_WEBSITE_URL`
- `VIREL_SUPPORT_EMAIL`
- `VIREL_SUPPORT_PAYMENT_URL`
- `VIREL_PURCHASE_URL` (optional during free public alpha)
- `VIREL_CLOUDFLARE_PAGES_PROJECT`
- `PHOTO_EDITOR_MACOS_SPARKLE_ARCHIVE` or `PHOTO_EDITOR_MACOS_SPARKLE_URL`
- `PHOTO_EDITOR_MACOS_APPCAST_URL`
- `PHOTO_EDITOR_MACOS_SPARKLE_PUBLIC_KEY`
- `PHOTO_EDITOR_WINDOWS_POSTGRES_RUNTIME_ARCHIVE` or `PHOTO_EDITOR_WINDOWS_POSTGRES_RUNTIME_URL`
- `PHOTO_EDITOR_WINDOWS_PACKAGE_NAME`
- `PHOTO_EDITOR_WINDOWS_PACKAGE_DISPLAY_NAME`
- `PHOTO_EDITOR_WINDOWS_PACKAGE_PUBLISHER`
- `PHOTO_EDITOR_WINDOWS_PUBLISHER_DISPLAY_NAME`
- `PHOTO_EDITOR_WINDOWS_DESCRIPTION`
- `PHOTO_EDITOR_WINDOWS_TIMESTAMP_URL`
- `PHOTO_EDITOR_WINDOWS_APPINSTALLER_URI`
- `PHOTO_EDITOR_WINDOWS_PACKAGE_BASE_URI`

## Release sequence

1. Finish all clean-machine proof for the candidate.
2. Commit the version bump and release notes with a clean worktree.
3. Push tag `vX.Y.Z`.
4. Approve the protected `production` environment after reviewing the quality gate.
5. Confirm both notarized DMGs, signed MSIX, app installer, appcast, checksums, manifest, and notices are present in the public release.
6. Confirm Cloudflare Pages deployed the matching website configuration.
7. Install from the public page and perform a final smoke test.
8. Announce the release only after the updater feeds and support contact have been verified from an external network.
