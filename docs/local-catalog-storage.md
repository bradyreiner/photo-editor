# Local Catalog Storage

PostgreSQL is the only production catalog authority. Framehouse either connects to the explicit
`PHOTO_EDITOR_DATABASE_URL` or manages a private loopback-only PostgreSQL cluster beneath
`PHOTO_EDITOR_APP_DATA_DIR/postgres`.

The managed layout contains cluster data, a permission-restricted state file, service logs, and
custom-format `pg_dump` backups. The packaged PostgreSQL runtime supplies `postgres`, `initdb`,
`pg_ctl`, `pg_dump`, and `pg_restore`. All callers use the shared Rust `CatalogRepository` contract backed by
`CatalogService`; no SQLite production repository or PostgreSQL-to-SQLite cutover exists.

See [local-postgres-service.md](local-postgres-service.md) for runtime commands and
[database-migrations.md](database-migrations.md) for schema workflow.
