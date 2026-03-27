# Docker Build

## Purpose

The Docker image is for:

- reproducible local setup
- CI validation
- headless native build checks
- Linux desktop build experiments
- local PostgreSQL-backed product development

It is **not** the final product packaging strategy for macOS and Windows.

## Build headless

```sh
docker build -t photo-editor:headless .
```

Or with Compose:

```sh
docker compose build dev
```

This builds:

- Rust workspace checks
- CMake native targets
- smoke tests

Default behavior sets `BUILD_DESKTOP=OFF`, so Qt is not required.

## Build Linux desktop variant

```sh
docker build --build-arg BUILD_DESKTOP=ON -t photo-editor:desktop .
```

Or with Compose:

```sh
docker compose build desktop-dev
```

## Interactive development

Use Compose services as disposable development shells:

```sh
docker compose run --rm dev
docker compose run --rm desktop-dev
```

Both services mount the repository into `/workspace` so local edits are visible inside the container.

## Database service

Start PostgreSQL locally:

```sh
docker compose up -d postgres
```

The development containers expose:

`PHOTO_EDITOR_DATABASE_URL=postgresql://photo_editor:photo_editor@postgres:5432/photo_editor`

On the host, PostgreSQL is published on port `5433` by default to avoid conflicting with an existing local database. You can override that with `PHOTO_EDITOR_POSTGRES_HOST_PORT`.

For runtime catalog setup, use the checked-in migration flow instead of relying only on container init scripts:

```sh
cd rust
cargo run -p photo-catalog --bin catalog_migrate
```

This installs Qt 6 packages and attempts a Linux desktop build of the QML shell.

## Product reality

For a real desktop product:

- Docker is good for reproducible engineering environments and Linux CI
- macOS binaries must be built and signed on macOS
- Windows binaries should be built and signed on Windows
- GPU, color management, and packaging must be validated on target operating systems

Use Docker to standardize development and test infrastructure, not to replace native release pipelines.
