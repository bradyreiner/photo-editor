# Photo Editor

Native desktop photo editor scaffold for a Lightroom-class product:

- `Qt 6 + QML` desktop UI
- `C++20` image engine
- `Rust` catalog, metadata, and workflow services
- `PostgreSQL` local-first catalog service scaffold
- managed and referenced libraries
- multi-drive aware asset tracking

Current production direction in code:

- `PostgreSQL`-backed catalog service for development and early product packaging
- Git-tracked SQL migrations in `rust/crates/catalog/migrations`
- app-owned runtime directories for logs, backups, and local support files
- app-managed local Postgres bootstrap when `PHOTO_EDITOR_DATABASE_URL` is not provided

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

## Run Natively

Install the macOS host dependencies first:

```sh
brew install cmake ninja rust qt
```

Then run the app with:

```sh
./scripts/run-native.sh
```

That script now:

- builds the Rust catalog binaries first
- prepares app runtime directories
- starts a local managed catalog database when no external DB URL is configured
- otherwise runs migrations against the configured database
- launches the desktop app

You can also double-click [PhotoEditor.command](/Users/bradyreiner/projects/PersonalProjects/photo-editor/PhotoEditor.command) in Finder to build and launch the app in Terminal.

## PostgreSQL

For external development databases, start PostgreSQL yourself:

```sh
docker compose up -d postgres
```

The default connection string is:

```sh
postgresql://photo_editor:photo_editor@localhost:5433/photo_editor
```

You can copy [.env.example](/Users/bradyreiner/projects/PersonalProjects/photo-editor/.env.example) into your own environment when the catalog service is wired up.

If `PHOTO_EDITOR_DATABASE_URL` is not set, the desktop app now defaults to a managed local Postgres instance under the app data directory. That path requires Postgres command-line tools (`initdb`, `pg_ctl`) to be available on `PATH` or exposed through `PHOTO_EDITOR_POSTGRES_BIN_DIR`.

Schema changes are versioned in Git as `sqlx` migration files under [rust/crates/catalog/migrations](/Users/bradyreiner/projects/PersonalProjects/photo-editor/rust/crates/catalog/migrations).

To apply migrations manually:

```sh
cd rust
cargo run -p photo-catalog --bin catalog_migrate
```

To inspect or manage the local bundled-style catalog service:

```sh
cd rust
cargo run -p photo-catalog --bin catalog_local_db -- status
cargo run -p photo-catalog --bin catalog_local_db -- ensure-running
cargo run -p photo-catalog --bin catalog_local_db -- stop
```

## Import a folder into PostgreSQL

After the database is running, you can import a folder with:

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
