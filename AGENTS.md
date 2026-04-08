# Repository Guidelines

## Project Structure & Module Organization
`apps/desktop/` contains the Qt 6 desktop shell: C++ controllers in `src/` and QML UI in `qml/components/` and `qml/workspaces/`. `engine/` holds the C++20 rendering and edit pipeline, with public headers in `engine/include/` and implementations in `engine/src/`. `rust/` is a Cargo workspace for catalog, jobs, metadata, and FFI crates. Native tests live in `tests/`, build helpers in `cmake/`, and operational docs and SQL schemas in `docs/`.

## Build, Test, and Development Commands
Use the preset-based CMake flow:

- `cmake --preset ci-headless && cmake --build --preset build-ci-headless` builds the non-Qt CI target set.
- `ctest --preset test-ci-headless --output-on-failure` runs headless native tests.
- `cmake --preset dev-macos-debug && cmake --build --preset build-dev-macos-debug` configures a local macOS desktop build.
- `./scripts/run-native.sh` is the fastest full-stack macOS path: Rust binaries, runtime directories, catalog bootstrap, and app launch.
- `cd rust && cargo test --workspace` runs Rust tests across all crates.
- `docker compose up -d postgres` starts the local PostgreSQL dependency on port `5433`.

## Coding Style & Naming Conventions
C++ uses C++20, namespaces like `photo_editor::pipeline`, `snake_case` functions/variables, and `PascalCase` types. Follow the existing 2-space indentation in C++ and CMake files. QML files use `PascalCase` filenames such as `ActionButton.qml` and 4-space indentation. Rust follows standard `rustfmt` style and `snake_case` module/function names. Keep warnings clean under `cmake/CompilerWarnings.cmake` (`-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion`).

## Testing Guidelines
Add native tests in `tests/` and name files `*_test.cpp`; register them in `tests/CMakeLists.txt` with `add_test(...)`. Prefer focused smoke or controller tests next to the subsystem you changed, and run the smallest relevant target before a full `ctest`. For Rust changes, add crate-level tests and run `cargo test --workspace`.

## Commit & Pull Request Guidelines
Recent history uses short, plain-English subjects (`fixed a crash`, `video timeline`) rather than strict Conventional Commits. Keep commit titles brief, imperative, and scoped to one change. PRs should summarize user-visible behavior, call out any schema or environment-variable changes, link the relevant issue, and include screenshots or short recordings for QML/UI updates. Do not commit secrets; use `.env.example` as the reference for local configuration.
