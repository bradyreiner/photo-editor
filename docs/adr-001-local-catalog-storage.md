# ADR 001: PostgreSQL Remains the Catalog Authority

Status: Superseded and corrected on 2026-08-12 after product-owner rejection of the SQLite cutover.

Date: 2026-07-29

## Context

The desktop product is local-first and its critical workflow is import, cull, batch edit, refine,
export, relaunch, backup, and recovery for one signed-in desktop user. The former Rust catalog
repository used PostgreSQL and the app managed a private server runtime, credentials, port,
startup, health, dump/restore, and repair lifecycle. That is a sound client/server database but
creates product work unrelated to catalog semantics: bundled binaries, a background service,
credential bootstrap, port/process failures, clean-machine setup, signing, update compatibility,
and platform-specific repair.

No measured requirement currently needs concurrent remote SQL clients, a shared server, or
PostgreSQL-specific query behavior. The repository boundary already isolates callers from storage
details.

## Corrected decision

PostgreSQL remains the sole production catalog authority. The application may use an explicitly
configured PostgreSQL service or its managed private loopback-only PostgreSQL runtime.

- SQLx PostgreSQL migrations are the schema authority.
- The managed runtime, credentials, startup, health, backup, repair, packaging, and clean-machine
  gates remain product requirements.
- Existing and new catalog, recipe, timeline, carousel, job, review, and migration-record features
  must work through PostgreSQL.
- SQLite may not become a production authority without a new explicit product decision.

## Correction evidence

The managed PostgreSQL manager, external URL contract, runtime packaging, `pg_dump` backups, and
normal helper routing were restored. A fresh isolated cluster initialized, applied all migrations
through 0012, reported healthy, served a catalog snapshot, and shut down cleanly. SQLite production
modules and the PostgreSQL-to-SQLite migrator were deleted.

## Consequences

The product retains PostgreSQL's intended concurrency, server semantics, migration behavior, and
operational model. Releases must carry and verify the PostgreSQL runtime and prove managed startup,
backup, restore, upgrade, and clean-machine behavior on every supported platform.

## Rejected alternatives

- Keep bundled PostgreSQL permanently: rejected because no demonstrated local workflow requires
  its service cost.
- Support PostgreSQL and SQLite as equal writable backends: rejected because it doubles migration,
  recovery, query, and release proof indefinitely.
- Store the catalog in ad hoc JSON/files: rejected because transactional migration, indexing,
  integrity, concurrent reads, and recovery are core requirements.
