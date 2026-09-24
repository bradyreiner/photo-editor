# Local PostgreSQL Service

PostgreSQL is Framehouse's catalog authority. If `PHOTO_EDITOR_DATABASE_URL` is present, the app
uses that PostgreSQL database and applies the checked SQLx migrations. Otherwise it starts a
private managed PostgreSQL cluster under the application-data directory.

The managed cluster binds only to `127.0.0.1`, selects an available private port, generates a local
credential stored in an owner-only state file, and creates a pre-migration `pg_dump` backup before
schema changes. Packaged builds include `postgres`, `initdb`, `pg_ctl`, `pg_dump`, `pg_restore`, and
`psql`; other PostgreSQL command-line and development tools are excluded from the release runtime.

Use `catalog_local_db ensure-running`, `status`, `connection-url --ensure-running`, and `stop` for
local administration. `scripts/run-native.sh` preserves an explicit external database URL and
otherwise ensures the managed service is ready before launching Framehouse.

The packaged-runtime proof scripts exercise pre-migration rollback, ordinary backup/restore,
corrupt-archive refusal, interrupted backup cleanup, failed-restore safety rollback, missing-media
relink and deduplication, managed-library relocation, and managed versus external authority. A
failed restore is never reported as harmless: the manager first takes a live safety backup, and
the proof injects a failure after the candidate archive has already modified the database before
requiring that safety archive to restore the exact newer live catalog.

Both Bash and PowerShell proofs also create a disposable second database, inject representative
legacy row corruption only after dropping the relevant `NOT VALID` constraints, and require asset,
review, relink, migration, export-job, timeline, and carousel readers to return a structured record
identity without panicking. `catalog_integrity` must enumerate every corrupt identity and
`--require-clean` must fail. The fixtures never modify the live catalog or the managed proof
database used for backup/restore assertions.
