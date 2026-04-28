# Build And Test Rules

- Prefer the checked-in presets and helper scripts over ad hoc build commands.
- Use `cmake --preset ci-headless` and `cmake --build --preset build-ci-headless` for headless native validation.
- Use `ctest --preset test-ci-headless --output-on-failure` for headless C++ coverage.
- Use `cd rust && cargo test --workspace` for Rust workspace validation.
- For macOS desktop work, prefer the smallest focused loop first:
  - `cmake --preset dev-macos-debug`
  - `cmake --build --preset build-dev-macos-debug --target app_bootstrap_test app_shell_controller_test desktop_shell_smoke_test`
  - `ctest --test-dir build/dev-macos-debug -R "desktop_shell_smoke_test|app_shell_controller_test|app_bootstrap_test" --output-on-failure`
- If you touch packaging, CI, or release scripts, call out validation gaps explicitly when you cannot run the full path locally.
