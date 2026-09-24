# Performance Artifact Retention

`perf-artifacts/` contains generated local diagnostics. It is ignored by Git and is not release
evidence by itself.

The default retention boundary is:

- keep unpinned artifacts for at most 14 days;
- keep at most 20 unpinned runs per benchmark category;
- never manage an entry that is not an immediate timestamped artifact directory;
- never follow or remove a symbolic link;
- retain any artifact containing `.retain` or `promotion-manifest.json`.

Run a non-mutating plan:

```sh
python3 -B scripts/perf/prune-performance-artifacts.py
```

Review `planned_removals`, then opt into deletion:

```sh
python3 -B scripts/perf/prune-performance-artifacts.py --apply
```

Release or promotion evidence must have a checked manifest, source revision, command, hardware,
input identities, and skip/fallback status. Copy it into the appropriate checked corpus/evidence
location or immutable external store before pruning, or pin it temporarily with `.retain`.
Pinning prevents deletion but does not promote the artifact or make it release-eligible.
