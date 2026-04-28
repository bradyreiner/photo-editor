---
paths:
  - "apps/desktop/**/*"
  - "tests/app_*"
  - "tests/desktop_*"
  - "tests/catalog_asset_model_test.cpp"
  - "tests/editor_preview_*"
---

# Desktop Qt And QML Rules

- Preserve the existing controller pattern: QML stays declarative and controllers own application state and orchestration.
- Keep QML component names in `PascalCase` and avoid leaking heavy business logic into bindings or JavaScript blocks.
- When touching `apps/desktop/src`, check whether a focused desktop smoke test or controller test should move with the change.
- When changing preview, render, or job state plumbing, also inspect the nearby tests under `tests/` for stale expectations.
