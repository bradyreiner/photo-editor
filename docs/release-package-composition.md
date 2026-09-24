# Release package composition

`scripts/package-composition-report.py` is the canonical inventory for a staged
Framehouse application or installer payload. It records every regular file,
installed bytes, deterministic raw-DEFLATE level-9 bytes, SHA-256, category,
largest files under both size measures, repeated basenames, and byte-identical
content groups. It inventories every symlink and rejects broken targets or any
target that escapes the staged package root. When `--artifact-dir` is supplied,
it also records exact final installer/archive bytes and hashes. Release jobs
must run it after platform deployment has assembled the final staging tree and
installer.

The category totals identify the catalog-helper fleet, bundled PostgreSQL,
Basic Qt Quick Controls, non-Basic Qt Quick Controls styles, Qt Quick Controls
core, macOS-only runtime components intentionally excluded from the product,
models, libraries, resources, configuration, documentation/licenses, and other
executables/data separately. These path-aware categories keep a large static
helper fleet or unused Qt feature from disappearing inside a generic
"executable" or "runtime library" total.

Example:

```sh
python3 -B scripts/package-composition-report.py \
  --root build/release-stage \
  --output release-evidence/package-composition.json \
  --commit "$GITHUB_SHA" \
  --artifact-dir build/release/package \
  --previous release-baselines/package-composition.json \
  --max-installed-bytes 2500000000 \
  --max-category-bytes catalog-helper=10000000 \
  --max-category-bytes postgres-runtime=70000000 \
  --max-category-bytes qt-quick-controls-non-basic-style=0 \
  --max-category-bytes qt-macos-pruned-runtime=0 \
  --max-growth-bytes 50000000 \
  --fail-on-duplicates
```

An intentional duplicate must be listed by exact SHA-256 and exact path set in
a JSON allowlist. Each exception requires a reason, owner, and non-expired ISO
date. Stale entries also fail so an allowlist cannot quietly become a permanent
baseline. Built-in structural explanations cover Apple's identical Qt privacy
manifest inside each independently signed framework bundle and PostgreSQL's IANA
timezone aliases; both remain reported with their reasons. Repeated basenames remain visible for review even when their
content differs; only identical unexplained bytes are an automatic failure.

The current size values in the example are illustrative, not approved budgets.
Release engineering must set budgets from a signed prior package on each platform
and retain both reports with the release manifest. Source-tree or developer-build
inventories do not substitute for a final staged-package report.

`--max-category-bytes CATEGORY=BYTES` is repeatable and records each ceiling,
measured total, and pass/fail result in `categoryBudgets`. The macOS release job
currently caps the single macOS multicall catalog helper at 10,000,000 installed
bytes, leaving regression margin above the 7,379,584-byte ad-hoc-signed binary;
the bundled PostgreSQL runtime is capped at 70,000,000 bytes; non-Basic Qt
Quick Controls styles are capped at zero; and the known-unreachable macOS PDF
reader, unsupported legacy image readers, virtual-keyboard, redundant GLib
network backend, and parallel OpenSSL TLS plug-in branches are capped at zero. Windows and the remaining package categories still require signed
baseline artifacts before their budgets can be approved.

The helper inventory is read from `cmake/CatalogHelpers.txt` by packaging,
verification, dispatch validation, and size accounting. This prevents a helper
from being required by a release proof but omitted from the bundle or budget.
macOS packages one `catalog_helper` Mach-O and exposes all 13 established command
names as contained relative symlinks; the binary dispatches from `argv[0]`, so
existing application and recovery-script contracts are unchanged. Windows keeps
the explicit helper executables until equivalent package proof exists. The macOS deployment
selects the Basic style in QML and removes non-Basic style plugins before
dependency traversal. After `macdeployqt`, it also removes Qt's PDF image-reader
plug-in and `QtPdf` framework (PDF export uses `QPdfWriter` and remains), the
unused virtual-keyboard plug-in/frameworks, unsupported ICNS/ICO/JPEG 2000/MNG/TGA/WBMP
readers, the redundant GLib network backend, and the parallel OpenSSL TLS plug-in
while retaining the supported GIF reader, native Apple network/TLS backends, and
the OpenSSL libraries required directly by this QtNetwork build. After all files, including
PostgreSQL resources, are installed, a shared inner-to-outer signer refreshes
every Mach-O leaf, nested bundle, and the outer ad-hoc seal. Developer ID release
signing reuses that same ordering and replaces the ad-hoc identity. The PostgreSQL relocator packages only the
six executable tools used by the app and proof suite, excludes development
archives/metadata, and retains server modules and support data. A representative
ad-hoc-signed packaging-scope stage measured 239,355,893 installed bytes and
85,542,735 raw-DEFLATE bytes across 1,187 regular files and 119 contained,
resolvable symlinks. The one catalog binary accounts for 7,379,584 installed
bytes, down 52,614,848 bytes from the former 59,994,432-byte helper fleet.
PostgreSQL accounts for 64,370,855 installed bytes, down 15,760,068 bytes from
the previous 80,130,923-byte stage. The exact multicall stage passed all 12
managed/external database backup, restore, migration, corruption, relink, and
authority lifecycle markers.
These numbers are an optimization baseline, not release-candidate evidence;
the release gate still requires a fresh build from the exact clean source revision.

`deflateBytes` is a stable per-file comparison measure, not a prediction of DMG
or MSIX compression. `artifactBytes` is the exact size of the distributable.
Keeping both prevents an opaque installer format from hiding which staged files
caused growth.

On macOS, composition success is followed by
`scripts/verify-macos-bundle-dependencies.sh`. That gate walks every Mach-O file,
resolves its loader, executable, and run-path dependencies, and rejects missing,
relative, developer-machine, or other non-system absolute load paths. Composition
and dependency closure are separate gates: a partial bundle can be small and free
of duplicate bytes while still being unable to launch.
