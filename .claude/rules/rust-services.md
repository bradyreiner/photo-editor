---
paths:
  - "rust/**/*.rs"
---

# Rust Services Rules

- Treat the Rust workspace as production code, not glue code.
- Prefer crate-local tests first, then widen to `cargo test --workspace` when a change crosses crate boundaries.
- Keep the Rust to C++ boundary stable and explicit. If a change affects FFI-facing behavior, call out downstream C++ verification needs.
- For observability work, keep the existing JSONL logs intact and layer OpenTelemetry on top instead of replacing the local diagnostics path.
