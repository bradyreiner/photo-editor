# Looksy

Native desktop visual workspace for photography, motion, and design:

- `Qt 6 + QML` desktop UI
- `C++20` image engine
- `Rust` catalog, metadata, and workflow services
- managed or externally configured PostgreSQL catalog
- managed and referenced libraries
- multi-drive aware asset tracking

Current production direction in code:

- PostgreSQL catalog authority for the desktop product
- Git-tracked SQLx PostgreSQL migrations
- app-owned runtime directories for logs, backups, and local support files
- managed loopback-only PostgreSQL runtime for self-contained desktop installs

Near-term product focus:

- complete the professional photo workflow around `import -> cull -> batch edit -> export`
- deepen library and batch workflow speed before broadening adjacent workspaces
- use masking, image quality, and catalog reliability as the main product differentiators

See [docs/next-level-roadmap.md](/Users/bradyreiner/projects/PersonalProjects/photo-editor/docs/next-level-roadmap.md) for the prioritized roadmap.

## Repo layout

- `apps/desktop`: desktop application shell and QML workspaces
- `engine`: native image/rendering core
- `rust`: Rust workspace for catalog and background services
- `cmake`: shared build modules
- `docs`: architecture, schemas, and product scaffolding
- `tests`: lightweight native tests
- `Dockerfile`: reproducible Linux builder for CI and local validation
- `docker-compose.yml`: dev containers plus PostgreSQL service

## Configure

Headless configure without Qt:

```sh
cmake --preset ci-headless
cmake --build --preset build-ci-headless
ctest --preset test-ci-headless
```

Desktop configure expects Qt 6 development packages to be installed.

The headless preset now skips Qt-dependent desktop tests so it can run in Linux CI and Docker without a Qt toolchain.

## Color validation

The color engine keeps proprietary production behavior while validating against pinned open
standards and independent open-source engines. It does not depend on Adobe APIs, SDKs, services, or
Adobe-generated fixtures. DNG documentation is used as a public interoperability specification;
the DNG/CIE oracles and conformance fixtures are project-authored. CI enforces this boundary across
dependency declarations, runtime sources, and color-fixture provenance with
`scripts/color/validate-proprietary-color-boundary.py`.

The deterministic chart gate covers 6 cameras, 2 controlled illuminants, and 4 exposures. Its
shipping desktop renders are compared with direct float-intermediate OpenColorIO ACES 2 references
using strict Delta E 00, Delta E ITP, alpha, hash, profile, recipe, and provenance contracts. See
[docs/color-engine-industry-leading-plan.md](/Users/bradyreiner/projects/PersonalProjects/photo-editor/docs/color-engine-industry-leading-plan.md)
for the measured results, exact remaining physical-device gates, and reproduction commands.
Release builds also publish pass-only rendering-migration proofs bound to the exact executable;
they prove that ACES 2 preview is non-mutating, apply changes only the rendering-model identity,
and undo, redo, and clean reload recover byte-identical recipes.

The candidate renderer can be promoted only by one fail-closed command over an evidence volume and
a clean checkout at the exact recorded Git revision:

```sh
python3 -B scripts/color/validate-color-default-promotion.py \
  /path/to/evidence/color-default-promotion.json \
  --repository-root . \
  --allowed-signers perf-corpora/color-validation-required/color-promotion-allowed-signers
```

That command reruns the Apple Metal/Core Image, Windows RAW, macOS and Windows migration, licensed
corpus, all-built-in-preset, physical Windows SDR/HDR display, and printer/paper validators. It also
regenerates the corpus aggregate and requires two final approvers who did not prepare any nested
evidence. No individual hardware or editorial artifact is allowed to promote the default alone.
Physical-output summaries are not trusted directly: v2 proofs hash the raw Lab/ICtCp patch data and
recompute CIEDE2000 and BT.2124 Delta E ITP percentiles and maxima during validation.
Import a lab or instrument worksheet that has been normalized to the documented canonical CSV
columns with:

```sh
python3 -B scripts/color/import-physical-color-measurements.py measurements.csv \
  --output measurements.json
```

The importer rejects reordered or missing columns, duplicate patches, non-finite samples, fewer
than 24 patches, and overwrites; its JSON output includes the exact hashes and recomputed values to
copy into the display or printer proof manifest.
Final approvals are OpenSSH detached signatures over the evidence hash, source revision, reviewer
identity, date, decision, categories, independence claim, and notes. The allowed-signers file must
already be committed at the recorded source revision; start from
`perf-corpora/color-validation-required/color-promotion-allowed-signers.example` and replace its
comment-only contents with the real reviewers' public keys before evidence collection. Generate
the canonical bytes to sign with `scripts/color/create-color-promotion-approval-statement.py`, then
sign them using the namespace `photo-editor-color-approval`; editing any signed field invalidates
promotion.

Promotion assembly is also fail-safe. Place every downloaded proof and executable inside one
evidence directory, then use `scripts/color/prepare-color-default-promotion.py --help` to create a
`pending-approval` draft. The preparer refuses files outside that directory, a dirty checkout,
missing inputs, existing output, or an unresolved revision and computes the complete evidence hash.
For each reviewer:

```sh
python3 -B scripts/color/create-color-promotion-approval-statement.py \
  --promotion-manifest /evidence/promotion-draft.json \
  --reviewer-record /evidence/reviewer-1.json \
  --output /evidence/reviewer-1.statement.json
ssh-keygen -Y sign -f /path/to/reviewer-key \
  -n photo-editor-color-approval /evidence/reviewer-1.statement.json
```

After two trusted reviewers sign independently, finalize without hand-editing the decision:

```sh
python3 -B scripts/color/finalize-color-default-promotion.py \
  --draft /evidence/promotion-draft.json \
  --approval /evidence/reviewer-1.json /evidence/reviewer-1.statement.json.sig \
  --approval /evidence/reviewer-2.json /evidence/reviewer-2.statement.json.sig \
  --allowed-signers perf-corpora/color-validation-required/color-promotion-allowed-signers \
  --repository-root . --output /evidence/color-default-promotion.json
```

Finalization writes nothing unless the complete v2 promotion validator passes.

## Run Natively

Install the macOS host dependencies first:

```sh
brew install cmake ninja rust qt
```

Then run the app with:

```sh
./scripts/run-native.sh
```

The launcher incrementally builds the Rust helpers and the `photo_editor_app`
target, reusing the existing CMake configuration. Tests and benchmarks are built
separately. It then prepares the existing PostgreSQL catalog and launches the app.

```sh
./scripts/run-native.sh --no-build           # Reopen the existing optimized app
./scripts/run-native.sh --release            # Build and launch the optimized app
./scripts/run-native.sh --release --no-build # Reopen the existing Release app
./scripts/run-native.sh --build-only --jobs 4 # Build without starting the app or database
./scripts/run-native.sh --configure          # Explicitly refresh CMake configuration
```

Release is the default so local photo editing uses optimized rendering. Use `--debug` for debugger sessions; use `--debug --no-build` to reopen that build.
`--no-build` requires an existing app and catalog helpers. App arguments can be
forwarded after `--`. The launcher waits up to ten seconds for an existing
Debug or Release instance from this checkout to exit before starting its replacement.
If shutdown stalls, it reports the problem without force-killing the app.

The launcher preserves `PHOTO_EDITOR_DATABASE_URL` and the existing runtime,
log, and backup directory overrides. Run the isolated launcher checks with
`python3 scripts/test-run-native.py`.

For focused thumbnail and recipe recovery tests, see [the editor stall verification notes](docs/editor-stall-fixes-2026-09-10.md#focused-local-commands).

You can also double-click [PhotoEditor.command](/Users/bradyreiner/projects/PersonalProjects/photo-editor/PhotoEditor.command) in Finder to build and launch the app in Terminal.

## Catalog storage

Without `PHOTO_EDITOR_DATABASE_URL`, the desktop app manages a private loopback-only PostgreSQL
cluster under the application-data directory. Packaged releases bundle the required PostgreSQL
runtime. See [docs/local-postgres-service.md](/Users/bradyreiner/projects/PersonalProjects/photo-editor/docs/local-postgres-service.md).

### External PostgreSQL compatibility

For external development databases, start PostgreSQL yourself:

```sh
docker compose up -d postgres
```

The default connection string is:

```sh
postgresql://photo_editor:photo_editor@localhost:5433/photo_editor
```

You can copy [.env.example](/Users/bradyreiner/projects/PersonalProjects/photo-editor/.env.example)
to configure an explicitly provisioned server. `PHOTO_EDITOR_DATABASE_URL` is honored by the
desktop app and every catalog helper; when it is absent, they use the managed local PostgreSQL
cluster instead.

Schema changes are versioned in Git as `sqlx` migration files under [rust/crates/catalog/migrations](/Users/bradyreiner/projects/PersonalProjects/photo-editor/rust/crates/catalog/migrations).

To apply migrations manually:

```sh
cd rust
cargo run -p photo-catalog --bin catalog_migrate
```

To inspect, back up, or restore the managed PostgreSQL catalog:

```sh
cd rust
cargo run -p photo-catalog --bin catalog_local_db -- status
cargo run -p photo-catalog --bin catalog_local_db -- ensure-running
cargo run -p photo-catalog --bin catalog_local_db -- backup
cargo run -p photo-catalog --bin catalog_local_db -- restore-latest
```

## Import a folder into the catalog

You can import a folder with:

```sh
cd rust
cargo run -p photo-catalog --bin import_catalog -- /path/to/photos drive-main "Main Drive" referenced
```

Use `managed` instead of `referenced` if you want the import record to reflect managed-library behavior.

## Docker

Headless Docker build:

```sh
docker build -t photo-editor:headless .
```

Linux desktop Docker build:

```sh
docker build --build-arg BUILD_DESKTOP=ON -t photo-editor:desktop .
```

Docker Compose:

```sh
docker compose build dev
docker compose build desktop-dev
```

Docker is intended for reproducible development and CI. Native macOS and Windows product builds still need platform-native pipelines for packaging, signing, and QA.

## Release Engineering

Repository automation now lives under `.github/workflows`:

- `ci.yml`: Linux headless build and test validation
- `security.yml`: Rust dependency audit plus secret scanning
- `release.yml`: tagged or manual native build packaging for macOS and Windows

These workflows provide the release backbone, but code signing, notarization, Sparkle appcasts, and Windows App Installer/MSIX publishing still require your real certificates and hosting configuration.

App/package versioning is now sourced from the root [`VERSION`](/Users/bradyreiner/projects/PersonalProjects/photo-editor/VERSION) file. Release tags must match it as `vX.Y.Z`.

For the full step-by-step shipping process, see [docs/shipping-guide.md](/Users/bradyreiner/projects/PersonalProjects/photo-editor/docs/shipping-guide.md).
For the short practical checklist, see [docs/release-checklist.md](/Users/bradyreiner/projects/PersonalProjects/photo-editor/docs/release-checklist.md).

To open an interactive shell in the container:

```sh
docker compose run --rm dev
docker compose run --rm desktop-dev
```
