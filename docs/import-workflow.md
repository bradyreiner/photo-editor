# Import Workflow

## Current implementation

The Rust catalog crate now supports a real discovery path for imports:

- recursively walk a folder
- keep only supported photo and RAW file extensions
- attempt EXIF metadata extraction
- fall back to filesystem timestamps when EXIF capture date is missing
- produce `CatalogAssetRecord` values ready for PostgreSQL persistence

## Supported metadata today

- capture year, month, day
- camera make
- camera model
- lens model
- ISO
- shutter speed
- aperture

## Next wiring step

The next application slice should:

1. invoke `discover_assets(...)` from a real import action
2. persist the returned `ImportSummary` through `CatalogService::import_summary(...)`
3. expose the resulting asset list to the Library workspace

## CLI ingest

There is now a minimal import command for backend validation:

```sh
cd rust
cargo run -p photo-catalog --bin import_catalog -- /path/to/photos drive-main "Main Drive" referenced
```
