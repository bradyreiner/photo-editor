# Local Postgres Service

The app now supports a managed local catalog database for the default desktop path.

## How it works

- If `PHOTO_EDITOR_DATABASE_URL` is set, the app uses that database and only runs schema migrations.
- If `PHOTO_EDITOR_DATABASE_URL` is not set, the app starts a private local Postgres cluster under the app data directory.
- The local service generates a per-install password and binds only to `127.0.0.1`.

## Runtime layout

Under `PHOTO_EDITOR_APP_DATA_DIR`:

- `postgres/data`: initialized Postgres cluster
- `postgres/logs/postgres.log`: local server log
- `postgres/state.json`: generated connection state for the local cluster

## Tooling requirement

During development, the helper expects `initdb` and `pg_ctl` to be available:

- on `PATH`, or
- under `PHOTO_EDITOR_POSTGRES_BIN_DIR`

For real production packaging, these binaries need to be bundled inside the app installer or companion runtime package.

Current expected packaged layout:

- macOS app bundle:
  - `PhotoEditor.app/Contents/MacOS/PhotoEditor`
  - `PhotoEditor.app/Contents/MacOS/catalog_local_db` and the other Rust helpers
  - `PhotoEditor.app/Contents/Resources/postgres/` for a bundled Postgres runtime root
- Windows install root:
  - `PhotoEditor.exe`
  - helper binaries next to the app executable
  - `postgres/` under the install `bin` directory for a bundled runtime root

The GitHub Actions release workflow now supports two Windows runtime supply modes:

- `PHOTO_EDITOR_WINDOWS_POSTGRES_RUNTIME_ARCHIVE`: path to a checked-out/runtime archive on disk
- `PHOTO_EDITOR_WINDOWS_POSTGRES_RUNTIME_URL`: URL to a zip or tar archive

The archive is normalized during CI so CMake always receives a runtime root containing `bin/initdb` and `bin/pg_ctl`.

## Helper commands

```sh
cd rust
cargo run -p photo-catalog --bin catalog_local_db -- status
cargo run -p photo-catalog --bin catalog_local_db -- ensure-running
cargo run -p photo-catalog --bin catalog_local_db -- connection-url --ensure-running
cargo run -p photo-catalog --bin catalog_local_db -- stop
```
