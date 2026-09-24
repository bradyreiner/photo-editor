# Source Reproducibility

Framehouse release builds use only files present in the Git commit being built. Local agent
integrations are deliberately outside that source boundary.

## Source ownership

| Surface | Owner | Release status |
| --- | --- | --- |
| Desktop automation and photo tools | `apps/desktop/src/photo_automation_bridge.*` and `photo_mcp_server.*` | Production source |
| Repository-development MCP server | `rust/crates/mcp` | Local-only; excluded from the Cargo workspace |
| Developer agent scripts | `scripts/ai` | Local-only; no tracked build or runtime references |
| Personal Codex plugin | `plugins/photo-editor-codex` | Local-only; no tracked build or runtime references |

The local-only paths stay ignored and are rejected if committed. Adding a release dependency on
one of them requires moving the complete implementation into tracked source, updating this table,
and passing the clean-source CI job.

## Generated artifacts

| Artifact | Generator and tracked inputs | Regeneration command | Committed |
| --- | --- | --- | --- |
| CMake/Ninja files and native objects | CMake plus tracked `CMakeLists.txt`, `cmake/`, C++, Objective-C++, and shader sources | `cmake --preset ci-headless && cmake --build --preset build-ci-headless` | No |
| Qt MOC, RCC, QML cache, and QML type-registration output | Qt 6 tools plus tracked desktop CMake, QML, resources, and headers | `cmake --preset dev-macos-debug && cmake --build --preset build-dev-macos-debug` | No |
| Rust build output | Cargo/rustc plus tracked manifests, lockfile, Rust sources, and migrations | `cargo build --manifest-path rust/Cargo.toml --workspace --locked` | No |
| App icons | `scripts/generate-app-icons.py` plus `apps/desktop/assets/app-icon-1024.png` | `python3 scripts/generate-app-icons.py` | Platform outputs are committed release assets |
| Packages and release manifests | CPack and scripts under `scripts/` plus the built app, Rust helpers, licenses, and configured platform runtimes | `scripts/build-release.sh` | No |

Generated build output must never satisfy a missing source dependency. A generated artifact needed
at configure or compile time is valid only when its generator and every input are tracked and the
artifact can be recreated inside the exported source tree.

## Clean-source proof

The `source-closure` CI job exports `HEAD` with `git archive` into a new empty directory. It then:

1. writes a SHA-256 inventory for every exported source file and the Cargo lockfile;
2. rejects missing or out-of-tree Cargo path dependencies, escaping symlinks, and path collisions
   that would alias on case-insensitive or Unicode-normalizing filesystems;
3. runs locked Cargo metadata, build, and tests from the export;
4. configures, builds, and tests the headless native targets from the export; and
5. publishes the source and build-target inventories as release-candidate evidence.

Platform release jobs remain responsible for strict macOS and Windows packaging, signing, runtime
dependency inspection, and install/launch verification. Those jobs consume the same tracked commit;
they must not copy source from a developer worktree.
