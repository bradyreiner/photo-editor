# Database Migrations

The catalog schema is now versioned through `sqlx` migration files in:

`rust/crates/catalog/migrations`

## Workflow

Create a new migration:

```sh
cd rust
cargo install sqlx-cli --no-default-features --features rustls,postgres
cargo sqlx migrate add <name>
```

Apply migrations locally:

```sh
cd rust
cargo run -p photo-catalog --bin catalog_migrate
```

## Source Of Truth

- Runtime schema changes come from the migration files.
- `docs/postgres-schema.sql` is the reviewed schema snapshot for humans and tooling.
- User catalog data is not version-controlled in Git.

## Rules

- Prefer forward-only migrations for shipped catalogs.
- Keep edit recipe versioning separate from raw schema evolution.
- Never rely on `docker-entrypoint-initdb.d` as the only migration path for production.
