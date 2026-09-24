# Catalog Migrations

The PostgreSQL catalog schema is versioned by SQLx migration files under
`rust/crates/catalog/migrations`. Managed and externally configured PostgreSQL databases apply the
same migrations before the repository becomes available.

## Workflow

Create a SQLx migration:

```sh
cd rust
cargo install sqlx-cli --no-default-features --features rustls,postgres
cargo sqlx migrate add <name>
```

Apply migrations:

```sh
cd rust
cargo run -p photo-catalog --bin catalog_migrate
```

## Source Of Truth

- SQLx migration files are the runtime source of truth.
- `docs/postgres-schema.sql` is the reviewed PostgreSQL schema snapshot for humans and tooling.
- User catalog data is not version-controlled in Git.

## Rules

- Prefer forward-only, idempotent migrations for shipped catalogs.
- Keep edit recipe versioning separate from raw schema evolution.
- Never rely on container initialization as the only production migration path.
