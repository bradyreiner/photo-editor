# Security Policy

## Reporting

If you discover a security issue, do not open a public issue with exploit details.

Report it privately to the maintainer with:

- affected version or commit
- reproduction steps
- impact assessment
- any proof-of-concept material

## Security priorities

- signed release artifacts
- notarized macOS distribution
- local-only database access in production
- no secrets or user catalogs committed to Git
- dependency auditing and secret scanning in CI
- careful handling of untrusted image and metadata inputs
