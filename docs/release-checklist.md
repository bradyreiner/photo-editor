# Virel Studio Release Checklist

Use [production-launch-checklist.md](production-launch-checklist.md) for the
one-time business, certificate, hosting, legal, and clean-machine launch gates.
Use this checklist for every shipped version.

## Prepare

- [ ] Worktree is clean and the release commit is pushed.
- [ ] `VERSION` contains `X.Y.Z` and release notes exist at `docs/releases/vX.Y.Z-alpha.md`.
- [ ] User-visible changes, known limitations, migration notes, and backup advice are documented.
- [ ] Payment, support, privacy, and download configuration remain accurate.
- [ ] No signing, payment, database, or model secret is present in source or artifacts.

## Candidate proof

- [ ] `cargo test --manifest-path rust/Cargo.toml --workspace --locked` passes.
- [ ] Headless native configure/build/CTest passes.
- [ ] Mask release gate passes with the materialized real-image corpus.
- [ ] Apple-silicon, Intel Mac, and Windows x64 packages build.
- [ ] Managed PostgreSQL runtime contains `initdb`, `pg_ctl`, `pg_dump`, and `pg_restore`.
- [ ] Import, cull, edit, mask, export, quit, relaunch, and catalog recovery pass on clean machines.
- [ ] Preview/export parity and target-hardware proof meet the release baseline.

## Publish

- [ ] Commit the version and notes, then push tag `vX.Y.Z`.
- [ ] Review and approve the protected `production` environment.
- [ ] Both DMGs pass Developer ID, Hardened Runtime, notarization, staple, and Gatekeeper assessment.
- [ ] MSIX passes trusted Authenticode signature and timestamp validation.
- [ ] Sparkle appcast contains EdDSA signatures for both Mac architectures.
- [ ] Public release contains installers, appcast, checksums, release manifest, license, notices, and dependency inventory.
- [ ] Public release repository remains public while the source repository remains private.
- [ ] Cloudflare Pages deploys the same version, support address, and hosted checkout links.

## Upgrade proof

- [ ] Existing macOS arm64 install updates through Sparkle.
- [ ] Existing macOS x86_64 install updates through Sparkle.
- [ ] Existing Windows install updates through App Installer.
- [ ] Versioned pre-migration dump is created before schema changes.
- [ ] Updated app retains catalog, recipes, preferences, exports, and update registration.
- [ ] Failed or unavailable update feed leaves the installed app usable.

## Final external check

- [ ] Download every platform build from the public website on an external network.
- [ ] SHA-256 matches `release-manifest.json` and `SHA256SUMS.txt`.
- [ ] Install dialogs identify the expected Virel publisher without trust warnings.
- [ ] Release notes, support, privacy, terms, refunds, and licenses links resolve.
- [ ] Optional support payment completes and produces a receipt/refund path.
- [ ] Announce only after support intake and updater feeds are live.
