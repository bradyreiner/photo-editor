use std::collections::BTreeMap;
use std::path::{Path, PathBuf};

use photo_catalog::{
    run_catalog_helper, AppRuntimePaths, CatalogAssetSnapshot, CatalogRepository, CatalogRuntime,
    RecipeMigrationRecord,
};
use serde::Serialize;
use serde_json::Value;

#[derive(Debug, Serialize)]
struct MigrationInventory {
    schema: &'static str,
    edited_asset_count: usize,
    assets: Vec<MigrationInventoryAsset>,
}

#[derive(Debug, Serialize)]
struct MigrationInventoryAsset {
    asset_id: String,
    source_path: PathBuf,
    source_available: bool,
    catalog_location_online: bool,
    recipe_authority: &'static str,
    source_renderer: String,
    legacy_recipe: Option<Value>,
    canonical_recipe_payload: Option<String>,
    canonical_cache_key: Option<String>,
    canonical_provenance_payload: Option<String>,
    migration_record: Option<RecipeMigrationRecord>,
}

pub(crate) fn main() -> std::process::ExitCode {
    run_catalog_helper(run)
}

async fn run() -> Result<(), Box<dyn std::error::Error>> {
    let catalog = CatalogRuntime::open(&AppRuntimePaths::from_env()).await?;
    let snapshots = catalog.list_asset_snapshots(i64::MAX).await?;
    let relinked_paths = catalog
        .list_asset_relinks()
        .await?
        .into_iter()
        .map(|record| (record.asset_id, PathBuf::from(record.relinked_file_path)))
        .collect();
    let migration_records = catalog
        .list_recipe_migration_records()
        .await?
        .into_iter()
        .map(|record| (record.asset_id.clone(), record))
        .collect();
    let inventory = build_inventory(snapshots, relinked_paths, migration_records);
    println!("{}", serde_json::to_string_pretty(&inventory)?);
    Ok(())
}

fn build_inventory(
    mut snapshots: Vec<CatalogAssetSnapshot>,
    relinked_paths: BTreeMap<String, PathBuf>,
    mut migration_records: BTreeMap<String, RecipeMigrationRecord>,
) -> MigrationInventory {
    snapshots.sort_by(|left, right| left.asset.asset_id.cmp(&right.asset.asset_id));
    let assets = snapshots
        .into_iter()
        .filter(|snapshot| {
            snapshot.edit_recipe.is_some() || snapshot.canonical_edit_recipe_v13.is_some()
        })
        .map(|snapshot| {
            let asset_id = snapshot.asset.asset_id.clone();
            let source_path = relinked_paths
                .get(&asset_id)
                .cloned()
                .unwrap_or_else(|| catalog_source_path(&snapshot));
            let canonical = snapshot.canonical_edit_recipe_v13;
            let source_renderer = snapshot
                .edit_recipe
                .as_ref()
                .and_then(legacy_renderer)
                .unwrap_or_else(|| {
                    if canonical.is_some() {
                        "aces2-ocio-v1".to_string()
                    } else {
                        String::new()
                    }
                });
            MigrationInventoryAsset {
                asset_id: asset_id.clone(),
                source_available: source_path.is_file(),
                source_path,
                catalog_location_online: snapshot.asset.location.online,
                recipe_authority: if canonical.is_some() {
                    "canonical-v13"
                } else {
                    "legacy-compatibility"
                },
                source_renderer,
                legacy_recipe: snapshot.edit_recipe,
                canonical_recipe_payload: canonical
                    .as_ref()
                    .map(|recipe| recipe.recipe_payload.clone()),
                canonical_cache_key: canonical.as_ref().map(|recipe| recipe.cache_key.clone()),
                canonical_provenance_payload: canonical.map(|recipe| recipe.provenance_payload),
                migration_record: migration_records.remove(&asset_id),
            }
        })
        .collect::<Vec<_>>();
    MigrationInventory {
        schema: "photo-editor-rendering-migration-inventory:v1",
        edited_asset_count: assets.len(),
        assets,
    }
}

fn catalog_source_path(snapshot: &CatalogAssetSnapshot) -> PathBuf {
    let root = Path::new(&snapshot.asset.location.root_path);
    if snapshot.asset.location.relative_path.is_empty() {
        return root.to_path_buf();
    }
    root.join(&snapshot.asset.location.relative_path)
}

fn legacy_renderer(recipe: &Value) -> Option<String> {
    recipe
        .get("colorPipeline")
        .and_then(|pipeline| pipeline.get("photographicRenderingModel"))
        .or_else(|| recipe.get("photographicRenderingModel"))
        .and_then(Value::as_str)
        .map(str::to_string)
}

#[cfg(test)]
mod tests {
    use photo_catalog::{AssetLocation, CanonicalEditRecipeV13, CatalogAssetRecord, ImportMode};
    use tempfile::tempdir;

    use super::*;

    fn snapshot(
        asset_id: &str,
        root_path: &Path,
        relative_path: &str,
        edit_recipe: Option<Value>,
        canonical: Option<CanonicalEditRecipeV13>,
    ) -> CatalogAssetSnapshot {
        CatalogAssetSnapshot {
            asset: CatalogAssetRecord {
                asset_id: asset_id.to_string(),
                capture_year: 2026,
                capture_month: 8,
                capture_day: 3,
                camera_make: "Test".to_string(),
                camera_model: "Camera".to_string(),
                lens_model: "Lens".to_string(),
                iso: Some(100),
                shutter_speed: "1/125".to_string(),
                aperture: Some(4.0),
                import_mode: ImportMode::Referenced,
                location: AssetLocation {
                    drive_id: "drive".to_string(),
                    root_path: root_path.display().to_string(),
                    relative_path: relative_path.to_string(),
                    online: true,
                },
            },
            edit_recipe,
            canonical_edit_recipe_v13: canonical,
            imported_at: "2026-08-03T00:00:00Z".to_string(),
        }
    }

    #[test]
    fn inventory_is_sorted_filtered_and_relink_aware() {
        let root = tempdir().expect("temporary inventory root");
        let relinked = root.path().join("relinked.dng");
        std::fs::write(&relinked, b"raw").expect("write relinked source");
        let snapshots = vec![
            snapshot("unedited", root.path(), "unedited.dng", None, None),
            snapshot(
                "legacy",
                root.path(),
                "missing.dng",
                Some(serde_json::json!({
                    "recipeVersion": 12,
                    "colorPipeline": {
                        "photographicRenderingModel": "legacy-oklch-v1"
                    }
                })),
                None,
            ),
            snapshot(
                "canonical",
                root.path(),
                "canonical.dng",
                None,
                Some(CanonicalEditRecipeV13 {
                    asset_id: "canonical".to_string(),
                    recipe_payload: "photo-editor-edit-recipe-v13.1".to_string(),
                    cache_key: "sha256:canonical".to_string(),
                    provenance_payload: photo_catalog::EMPTY_CANONICAL_RECIPE_PROVENANCE_V1
                        .to_string(),
                    updated_at: "2026-08-03T00:00:00Z".to_string(),
                }),
            ),
        ];
        let inventory = build_inventory(
            snapshots,
            BTreeMap::from([("legacy".to_string(), relinked.clone())]),
            BTreeMap::new(),
        );
        assert_eq!(inventory.edited_asset_count, 2);
        assert_eq!(inventory.assets[0].asset_id, "canonical");
        assert_eq!(inventory.assets[0].recipe_authority, "canonical-v13");
        assert_eq!(inventory.assets[0].source_renderer, "aces2-ocio-v1");
        assert_eq!(
            inventory.assets[0].canonical_provenance_payload.as_deref(),
            Some(photo_catalog::EMPTY_CANONICAL_RECIPE_PROVENANCE_V1)
        );
        assert!(!inventory.assets[0].source_available);
        assert_eq!(inventory.assets[1].asset_id, "legacy");
        assert_eq!(inventory.assets[1].recipe_authority, "legacy-compatibility");
        assert_eq!(inventory.assets[1].source_renderer, "legacy-oklch-v1");
        assert_eq!(inventory.assets[1].source_path, relinked);
        assert!(inventory.assets[1].source_available);
    }
}
