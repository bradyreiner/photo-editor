# Framehouse Rename Implementation Plan

> Historical plan for an earlier naming direction. Looksy is the current
> product brand; use [looksy-testing-and-distribution.md](looksy-testing-and-distribution.md)
> and [production-launch-checklist.md](production-launch-checklist.md) for the
> active release path. Framehouse references below are retained as history and
> compatibility context, not current product copy.

## Objective

Replace the unfinished Stillfold/Virel public identity with **Framehouse** across the desktop app, installers, release pipeline, website, support material, and tests without breaking existing catalogs, preferences, automation integrations, or private-alpha upgrades.

The rename is complete only when a packaged build presents Framehouse consistently, old user data opens in place, release automation resolves the new artifact names, and no obsolete public-brand surface remains outside an explicit compatibility or historical allowlist.

## Naming contract

- Public product name: `Framehouse`
- Public product slug and executable name: `Framehouse`
- Written form: one word, capital `F`; never `Frame House`
- Public descriptor: `the visual workspace`
- Workspace names remain functional: `Library`, `Studio`, `Timeline`, `Carousel`, `Export`
- Product-qualified names use `Framehouse Studio`, `Framehouse Timeline`, and `Framehouse Catalog` only where qualification improves clarity.

## Rename boundaries

### Rename now

- User-visible Stillfold and Virel product copy.
- Window, About, first-run, preferences, import, agent, timeline-review, support, and error text.
- Brand QML component names and accessibility labels.
- Logos, wordmarks, icons, website imagery, installer artwork, and filenames.
- App bundle, executable, DMG, ZIP, MSIX, App Installer, and release artifact display names.
- Product metadata, public URLs, release titles, update-feed asset matching, and public documentation.
- Public release configuration currently named after Virel.

### Preserve for compatibility

- C++ namespaces and include paths under `photo_editor`.
- CMake target names such as `photo_editor_app` and the QML module URI `PhotoEditor`.
- Rust crate names, helper binary names, SQL database/user names, and existing migrations.
- Existing `PHOTO_EDITOR_*` runtime and developer environment variables.
- Existing application-data and preference identity in `AppBootstrap::application_name` and `organization_name` so QSettings and `QStandardPaths` continue to resolve the current data.
- Existing machine-readable recipe, preset, MIME, automation, and resource identifiers unless they are explicitly versioned and migrated.
- Repository directory and GitHub source-repository name; these are engineering identifiers, not required public branding.

Do not run a repository-wide `photo_editor -> framehouse` replacement. That would create a large, risky ABI, storage, protocol, and migration change with no user benefit.

## Gate 0: identity and release decisions

- [ ] Complete a trademark and marketplace clearance review for `Framehouse` before publishing a renamed build.
- [ ] Decide whether `Framehouse` is only the product name or also the legal publisher/vendor name.
- [ ] Decide the permanent website domain, support email, public release repository, and social/store handles.
- [x] Decide bundle/package identity before the first public release:
  - Recommended for upgrade continuity: keep the existing macOS bundle identifier and Windows package identity if either has shipped to testers.
  - If a clean permanent identity is required, change it once before public release and implement an explicit old-data discovery/migration test.
- [ ] Confirm whether existing Sparkle keys/feed and Windows signing certificate remain authoritative; a display-brand change must not silently break signed upgrades.
- [ ] Approve the Framehouse wordmark, mark, app icon, light/dark variants, and minimum-size behavior.
- [ ] Approve the agent character direction separately from the brand mark. The current character inherits the Stillfold folded-corner logo, so a text-only rename is insufficient.
- [x] Record the implementation-safe decisions above in this document before changing package identities or legal copy.

Implementation decision: preserve `com.virel.studio`, the existing Windows package identity/application ID, application-data identity, signing inputs, update keys, and machine-readable integration identifiers for continuity. Only public names, artwork, descriptions, executable/artifact names, and deployment-variable names change. The legal publisher, permanent public endpoints, trademark clearance, and certificate authority remain release gates rather than code assumptions.

Preliminary marketplace screen, 2026-07-19: **not cleared for publication**. An exact-name [Framehouse app is already listed by Futura Ventures Global LLC](https://apps.apple.com/ng/app/framehouse/id6758853062), is compatible with Apple-silicon Macs, and uses uploaded photos for an AI visual-design workflow. Its public product site is [framehouse.design](https://framehouse.design/). Exact-name photography/video businesses also operate at [framehouse.at](https://framehouse.at/) and under FrameHouse Studio. This is not a legal conclusion, but it is a material same-name marketplace collision. Do not publish or file under Framehouse until qualified counsel reviews the relevant software, design, photography, and media classes and the product owner explicitly accepts the risk or chooses a new name.

## Phase 1: centralize product identity

Goal: make one reviewed configuration drive the app and packaging instead of repeating brand strings.

- [x] Replace the public `VIREL_PRODUCT_*` CMake settings in `CMakeLists.txt` with centralized `FRAMEHOUSE_*` metadata for product name, slug, legal publisher, support URL, privacy URL, and terms URL. The legal publisher remains a fail-closed release placeholder until Gate 0 is decided.
- [x] Update compile definitions and `AppShellController` fallbacks to consume the centralized Framehouse metadata.
- [x] Keep `PHOTO_EDITOR_*` engineering/runtime settings unchanged.
- [x] Add source-level brand contract coverage that validates the canonical spelling and metadata values.
- [x] Add a documented compatibility policy for old `VIREL_*` release variables:
  - Prefer new neutral or `FRAMEHOUSE_*` deployment variables.
  - Accept old names as temporary aliases only if current external CI/site configuration depends on them.
  - Fail if both old and new names are set to conflicting values.
- [x] Update `THIRD_PARTY_NOTICES.md` product attribution without changing third-party license text.

Proof:

- Configure output reports `Framehouse` as the public product and artifact slug.
- `AppShellController::productName()` returns `Framehouse`.
- The brand contract test passes without depending on QML rendering.

## Phase 2: replace the in-app brand system

Goal: remove the Stillfold name and fold motif from every visible desktop surface.

- [x] Replace and rename these brand components and their CMake QML registrations:
  - `StillfoldMark.qml` -> `FramehouseMark.qml`
  - `StillfoldLetterhead.qml` -> `FramehouseLetterhead.qml`
  - `StillfoldAgent.qml` -> `FramehouseAgent.qml`
  - `StillfoldAgentDock.qml` -> `FramehouseAgentDock.qml`
- [x] Update `BrandWordmark.qml` to render `Framehouse` and replace its Stillfold-specific lockup contract.
- [x] Remove fold-specific identity comments, paths, colors, and contract strings from `Theme.qml` and the agent artwork.
- [x] Use an abstract frame/viewfinder mark rather than a literal house outline.
- [x] Replace visible copy in `Main.qml`, startup/first-run surfaces, preferences, import, Studio/Edit, Timeline, agent walkthroughs, review packages, support bundles, preset labels, errors, and accessibility text.
- [x] Decide whether product-owned areas retain descriptive names:
  - `Stillfold Vault` should become either `Framehouse Vault` or a neutral `Managed Library`.
  - `Stillfold Agent` should become either `Framehouse Agent` or a neutral `Assistant`.
  - Built-in presets should use `Framehouse` only if the brand label is useful in the preset browser.
  - Implemented decision: use `Framehouse Vault`, `Framehouse Agent`, and `Ask Framehouse`; retain branded preset names only where they identify a bundled look.
- [x] Rename support-bundle filenames and other newly generated user-facing filenames to Framehouse.
- [x] Update tests that assert exact brand copy, QML type names, source contracts, accessibility labels, and history labels.

Proof:

- Launch screenshots cover startup, main navigation, About, preferences, import, Studio agent, Timeline review import, and export.
- Keyboard navigation and screen-reader names contain Framehouse and no Stillfold references.
- QML module packaging succeeds after component file/type renames.
- Focused desktop shell, app shell, agent walkthrough, editor preview, and timeline tests pass.

## Phase 3: create and ship Framehouse assets

Goal: ensure no renamed build contains obsolete or placeholder Stillfold/Virel artwork.

- [x] Replace `stillfold-mark.svg`, `stillfold-logo.svg`, and `stillfold-letterhead.svg` with Framehouse source assets.
- [x] Regenerate `app-icon-1024.png`, macOS iconset/ICNS, Windows ICO/resource files, installer/store images, favicon, and Open Graph image from the approved source.
- [x] Rename asset-generation script descriptions, inputs, outputs, and preflight requirements.
- [x] Remove superseded Stillfold and Virel assets only after every build, test, packaging, and site reference points to Framehouse.
- [ ] Complete platform review of icons at 16, 32, 44, 128, 256, 512, and 1024 pixels, including Windows unplated and dark-background rendering. Generated dimensions and macOS/site previews are verified; native Windows rendering remains open.

Proof:

- macOS Finder/Dock/About and Windows Start menu/installer show the approved Framehouse icon.
- A repository search finds no current build reference to a Stillfold/Virel brand asset.
- Asset generation is deterministic from committed source artwork.

## Phase 4: rename packaging and release automation

Goal: produce installable, updateable Framehouse artifacts on both platforms.

- [x] Update `apps/desktop/Info.plist.in`:
  - display and bundle names;
  - icon filename;
  - speech-recognition usage text;
  - custom support/privacy/terms keys, preferably changing them to neutral names while retaining read compatibility if code consumes the old keys.
- [x] Update `apps/desktop/CMakeLists.txt` and `cmake/Packaging.cmake` for the Framehouse bundle, executable, resource paths, package description, and deploy-script naming.
- [x] Update macOS build, package, verify, appcast, and performance scripts to locate `Framehouse.app` and `Framehouse-*` artifacts.
- [x] Update Windows resource files and `scripts/package-windows-msix.ps1` defaults, executable path, application ID/display name, description, MSIX filename, and `Framehouse.appinstaller` filename.
- [x] Update `.github/workflows/release.yml` artifact names, release titles, glob patterns, public-release assertions, and site-deployment configuration.
- [x] Preserve signing identities and update keys unless Gate 0 explicitly authorizes a migration.
- [x] Update appcast/download asset matching atomically with artifact renaming so published downloads do not disappear.
- [x] Remove hardcoded `VirelStudio.appinstaller` and Stillfold artifact assertions from release verification.

Proof:

- A macOS package contains `Framehouse.app/Contents/MacOS/Framehouse`; plist inspection shows the approved display name, identifier, icon, permissions copy, and version.
- Gatekeeper/code-signature and packaged-runtime verification pass.
- A Windows package manifest names Framehouse, launches the correct executable, and retains the approved package identity/publisher.
- The App Installer resolves the renamed MSIX and performs an upgrade from the previous test build when continuity is required.

## Phase 5: update website, policy, and release documentation

Goal: make every public touchpoint agree with the packaged application.

- [x] Replace Virel Studio/Stillfold copy in `site/`, including titles, metadata, accessibility labels, screenshot captions, footers, download configuration, and JavaScript globals.
- [x] Replace Virel-named website images and cache-busting references.
- [x] Update `scripts/configure-release-site.py` product name and artifact patterns.
- [ ] Update privacy, terms, refunds, and support pages with legally correct product and publisher wording; do not mechanically replace a legal entity name with a product name.
- [x] Update release notes, shipping guide, release process, release checklists, production launch checklist, local Postgres packaging examples, and current product-facing roadmap titles.
- [x] Leave historical engineering records unchanged when rewriting them would falsify what an older build was called; mark them historical or add an allowlist entry instead.
- [x] Replace the public README heading and product description while retaining technical repo instructions and `PHOTO_EDITOR_*` examples.

Proof:

- Static-site inspection shows Framehouse in page title, metadata, navigation, download cards, policies, and accessibility labels.
- Generated download configuration resolves Framehouse artifacts for macOS and Windows.
- Link checking passes for download, support, privacy, terms, refunds, licenses, and release notes.

## Phase 6: compatibility and migration coverage

Goal: prove that the brand change does not strand existing work or break integrations.

- [x] Seed an old-version app data directory containing catalog state, preferences, presets, queued jobs, backups, and agent model configuration; construct the Framehouse bootstrap/controllers and verify the data is consumed or preserved in place.
- [x] Verify `AppBootstrap` continues resolving the existing application-data, cache, log, and backup roots.
- [x] Verify QSettings retains workspace, theme, preview backend, and first-run state under the preserved `PhotoEditor` organization/application identity.
- [x] Keep existing recipe/preset formats, MIME types, resource prefixes, automation protocol names, socket names, and `PHOTO_EDITOR_*` variables stable unless separately versioned.
- [x] Version the Stillfold review-note contract deliberately:
  - readers accept the existing `stillfold-review-notes` payload;
  - new writers emit an approved Framehouse or neutral format identifier;
  - round-trip and old-fixture tests prove compatibility.
- [ ] Verify an existing signed private-alpha build can update to Framehouse when the update channel is intended to continue.
- [ ] Verify uninstall/reinstall does not create a second empty catalog beside the old one.

Proof:

- Automated migration test opens pre-rename fixtures and preserves counts, recipes, project membership, preferences, and job state.
- Automation and MCP smoke tests pass without client configuration changes.
- Update rehearsal and clean-install rehearsal are recorded separately.

## Phase 7: brand-completeness gate

Goal: prevent old and new public identities from shipping together.

- [x] Add a brand-surface verification script that scans user-visible app, packaging, release, and site paths for `Stillfold`, `stillfold`, `Virel Studio`, and obsolete Virel asset names.
- [x] Maintain a narrow reviewed allowlist for compatibility keys and historical documents; every allowlist entry must state why it cannot yet be removed.
- [x] Run the checker in CI and release preflight.
- [x] Update exact-copy contract tests for the implemented naming and design contracts.
- [x] Run `git diff --check` and review all rename/delete pairs to ensure assets and QML types were not accidentally duplicated.

Final acceptance:

- [ ] Fresh macOS and Windows installs present Framehouse consistently.
- [ ] In-place upgrade preserves existing user data and update trust.
- [x] App, installer metadata, website, policies, release notes, and support bundle agree on the name in repository and generated macOS surfaces.
- [x] No obsolete Stillfold fold motif remains in visible UI or shipped artwork.
- [x] No unapproved package-identity, certificate, storage-path, schema, namespace, or protocol migration occurred.
- [ ] Trademark, publisher, domain, and legal-policy gates are recorded as complete before public release.

## Implementation evidence — 2026-07-19

Repository implementation is complete for the public rename, centralized metadata, QML component rename, non-fold brand artwork, generated platform/site assets, packaging metadata, release/site automation, current product documentation, legacy review-note reader, and brand-completeness enforcement. Compatibility-sensitive engineering identities remain unchanged.

Verified locally:

- `cmake --preset dev-macos-debug` configured with `Framehouse` metadata.
- `cmake --build --preset build-dev-macos-debug --target photo_editor_app app_shell_controller_test app_bootstrap_test agent_walkthrough_contract_test timeline_document_controller_test --parallel 8` built `Framehouse.app` and focused test binaries.
- `app_shell_controller_test`, `app_bootstrap_test`, `agent_walkthrough_contract_test`, and `timeline_ui_consistency_contract_test` pass.
- `app_shell_controller_test` now writes pre-rename QSettings keys under the preserved `PhotoEditor` identity and proves Framehouse restores workspace, theme, first-run, and preview-backend preferences.
- `app_bootstrap_test` now seeds representative PostgreSQL state, preset, queued-job, backup, and agent-model files and proves Framehouse bootstrap preserves every fixture byte-for-byte in the existing runtime root.
- The timeline controller test passes the Framehouse writer and legacy `stillfold-review-notes` import assertions before a later, unrelated Fit to Fill persisted-export assertion fails in the pre-existing dirty timeline work.
- The broader desktop source-contract test currently flags a pre-existing raw control in `StudioWorkspace.qml`; the Framehouse brand contract and QML packaging checks pass independently.
- `python3 -B scripts/verify-brand-surface.py` passes.
- The brand verifier also rejects `PhotoEditor` when it appears inside public C++ string literals while narrowly allowing the internal QML module identity. This caught and removed legacy `PhotoEditor` GPU-fallback copy and the `PhotoEditor Exports` folder name.
- `python3 -B scripts/validate-release-site.py site` passes.
- `python3 -B scripts/release-preflight.py --mode candidate --allow-dirty` passes.
- Public preflight now fails closed unless `FRAMEHOUSE_LEGAL_PUBLISHER` is configured, and policy pages render that exact value instead of treating the Framehouse product name as a legal entity.
- Production CMake packaging and Windows MSIX packaging also fail closed when the approved legal publisher is absent. Candidate bundles and policy pages visibly use `Publisher identity pending release approval` rather than inventing a copyright owner.
- Browser verification confirms the policy fallback renders clearly and a configured publisher value replaces every marked legal-identity occurrence.
- Python compilation, shell syntax checks, `actionlint`, and `git diff --check` pass.
- Deterministic asset generation produced the required PNG, ICNS, ICO, Windows Store/MSIX, favicon, and Open Graph dimensions; the macOS and site artwork were visually inspected.
- Runtime accessibility inspection confirms the Framehouse window title, wordmark, About surface, menu names, assistant labels, and preserved recovery of the existing application-data identity.
- CPack produced `build/dev-macos-debug/Framehouse-0.1.0-Darwin-arm64.dmg` with SHA-256 `841dbf34d0082b353cf6e047129ed7fafc4b763648d6cc737c54d276a0c5b2ee`. Read-only inspection confirms `Framehouse.app/Contents/MacOS/Framehouse`, the Framehouse icon/name, preserved bundle identifier `com.virel.studio`, and the fail-closed candidate publisher placeholder.
- The repository macOS verifier validates the DMG checksum and structure. Its production-runtime stage correctly rejects this development package because it does not bundle PostgreSQL.
- The full release path builds the self-contained Rust helpers, then stops at the intentional
  production gate because authoritative Sparkle distribution/feed/key configuration is not
  available locally.

Open release gates requiring product, legal, credential, or Windows ownership:

- trademark/marketplace clearance, resolution of the exact-name Framehouse app/design-platform collision recorded in Gate 0, legal publisher decision, permanent domain/support/repository/handle decisions, and qualified policy review;
- final visual approval for the mark, wordmark, agent, and native Windows icon rendering;
- production Sparkle feed/key and signing/notarization credentials;
- old signed-build-to-Framehouse updater rehearsal and uninstall/reinstall data-root rehearsal;
- native Windows MSIX build, install, icon, and in-place-upgrade verification;
- fresh clean-machine macOS and Windows acceptance passes.

## Recommended implementation sequence

Keep the work reviewable in these slices:

1. Identity decisions and centralized metadata.
2. Framehouse visual assets and QML component rename.
3. User-visible copy and exact-copy tests.
4. macOS/Windows packaging and release automation.
5. Website, policies, and product-facing documentation.
6. Compatibility fixtures, update rehearsal, and brand-completeness CI gate.

Do not delete legacy assets or compatibility identifiers in the same change that introduces their replacements. First prove the new references, then remove only the genuinely obsolete files in a narrow cleanup change.
