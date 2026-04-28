---
paths:
  - ".github/workflows/**/*.yml"
  - "cmake/Packaging.cmake"
  - "docs/release-checklist.md"
  - "docs/shipping-guide.md"
  - "scripts/*release*.sh"
  - "scripts/*notarize*.sh"
  - "scripts/*verify*.sh"
---

# Release And Ops Rules

- Treat signing, notarization, and packaging flows as high-risk. Prefer additive changes and call out any manual secrets or machine prerequisites.
- CI changes should improve confidence for the exact surface being edited. If a lane cannot cover the risk, note the remaining gap.
- Do not automate destructive release or signing commands without an explicit human decision point.
