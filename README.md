# Photo Editor

Native desktop photo editor scaffold for a Lightroom-class product:

- `Qt 6 + QML` desktop UI
- `C++20` image engine
- `Rust` catalog, metadata, and workflow services
- `SQLite` local-first catalog design
- managed and referenced libraries
- multi-drive aware asset tracking

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

## Run Natively

Install the macOS host dependencies first:

```sh
brew install cmake ninja rust qt
```

Then run the app with:

```sh
./scripts/run-native.sh
```

That script now builds the Rust catalog binaries first so the desktop app can read live catalog snapshots without spawning a fresh `cargo run` on every refresh.

You can also double-click [PhotoEditor.command](/Users/bradyreiner/projects/PersonalProjects/photo-editor/PhotoEditor.command) in Finder to build and launch the app in Terminal.

## PostgreSQL

Start the local product-style database:

```sh
docker compose up -d postgres
```

The default connection string is:

```sh
postgresql://photo_editor:photo_editor@localhost:5433/photo_editor
```

You can copy [.env.example](/Users/bradyreiner/projects/PersonalProjects/photo-editor/.env.example) into your own environment when the catalog service is wired up.

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

To open an interactive shell in the container:

```sh
docker compose run --rm dev
docker compose run --rm desktop-dev
```
