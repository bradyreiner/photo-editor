# Looksy Release Checklist

The first paid release targets **Apple silicon Macs**, with a one-time license
for the purchased major version through Lemon Squeezy. See
[commercial-launch.md](commercial-launch.md). Intel and Windows items below
apply when those platforms are added to `release/product.json`; they are deferred
for the first paid release.


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
- [ ] Packages build for every selected platform.
- [ ] Packages contain the licensed self-contained PostgreSQL runtime and no rejected SQLite authority;
  the managed catalog passes status, backup, restore, and rollback smoke tests, while external
  PostgreSQL permits backup but refuses unauthorized automatic restore.
  Run `PHOTO_EDITOR_CATALOG_BIN_DIR=<installed-helper-dir>
  PHOTO_EDITOR_POSTGRES_RUNTIME_DIR=<installed-runtime-root>
  scripts/test-postgres-catalog-runtime.sh` against the staged package.
  `verify-macos-release.sh` runs this lifecycle automatically against the helpers and PostgreSQL
  runtime mounted from the candidate DMG. The Windows release workflow runs the equivalent
  `test-postgres-catalog-runtime.ps1` contract against the staged install root before MSIX creation.
- [ ] The exact candidate catalog helpers pass the bounded 100,000-asset PostgreSQL proof and its
  create-only JSON report is retained with the release evidence. Run
  `python3 scripts/test-postgres-large-library.py --catalog-bin-dir <installed-helper-dir>
  --postgres-runtime-dir <installed-runtime-root> --output <new-report.json>`. This synthetic scale
  proof does not replace physical slow-drive, long-session, or clean-machine workflow evidence.
- [ ] Import, cull, edit, mask, export, quit, relaunch, and catalog recovery pass on clean machines.
- [ ] Preview/export parity and target-hardware proof meet the release baseline.

## Publish

- [ ] Commit the version and notes, then push tag `vX.Y.Z`.
- [ ] Review and approve the protected `production` environment.
- [ ] Every selected Mac DMG passes Developer ID, Hardened Runtime, notarization, staple, and Gatekeeper assessment.
- [ ] MSIX passes trusted Authenticode signature and timestamp validation.
- [ ] Sparkle appcast contains EdDSA signatures for both Mac architectures.
- [ ] Public release contains installers, appcast, checksums, release manifest, license, notices, and dependency inventory.
- [ ] Public release repository remains public while the source repository remains private.
- [ ] Cloudflare Pages deploys the same version, support address, and hosted checkout links.

## Upgrade proof

- [ ] Existing macOS arm64 install updates through Sparkle.
- [ ] Existing macOS x86_64 install updates through Sparkle.
- [ ] Existing Windows install updates through App Installer.
- [ ] Versioned PostgreSQL custom-format backup is created before schema changes.
- [ ] Updated app retains catalog, recipes, preferences, exports, and update registration.
- [ ] Failed or unavailable update feed leaves the installed app usable.

## Final external check

- [ ] Download every platform build from the public website on an external network.
- [ ] SHA-256 matches `release-manifest.json` and `SHA256SUMS.txt`.
- [ ] Install dialogs identify the expected Looksy publisher without trust warnings.
- [ ] Release notes, support, privacy, terms, refunds, and licenses links resolve.
- [ ] A real purchase completes, its receipt and downloads can be recovered, and a refund can be processed.
- [ ] Announce only after support intake and updater feeds are live.
