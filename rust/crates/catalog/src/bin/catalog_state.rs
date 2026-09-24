use std::env;

use photo_catalog::{
    run_catalog_helper, AppRuntimePaths, AssetReviewStateRecord, CatalogRepository, CatalogRuntime,
    RecipeMigrationRecord,
};

pub(crate) fn main() -> std::process::ExitCode {
    run_catalog_helper(run)
}

async fn run() -> Result<(), Box<dyn std::error::Error>> {
    let arguments: Vec<String> = env::args().collect();
    let command = arguments.get(1).map(String::as_str).unwrap_or("");

    let catalog = CatalogRuntime::open(&AppRuntimePaths::from_env()).await?;

    match command {
        "create-project" => {
            let project_name = required_arg(&arguments, 2, "missing project_name argument")?;
            catalog.upsert_project(project_name).await?;
        }
        "add-asset-to-project" => {
            let asset_id = required_arg(&arguments, 2, "missing asset_id argument")?;
            let project_name = required_arg(&arguments, 3, "missing project_name argument")?;
            catalog.add_asset_to_project(asset_id, project_name).await?;
        }
        "remove-asset-from-project" => {
            let asset_id = required_arg(&arguments, 2, "missing asset_id argument")?;
            let project_name = required_arg(&arguments, 3, "missing project_name argument")?;
            catalog
                .remove_asset_from_project(asset_id, project_name)
                .await?;
        }
        "delete-asset" => {
            let asset_id = required_arg(&arguments, 2, "missing asset_id argument")?;
            catalog.delete_asset(asset_id).await?;
        }
        "set-review-state" => {
            let asset_id = required_arg(&arguments, 2, "missing asset_id argument")?;
            let rating = required_arg(&arguments, 3, "missing rating argument")?.parse::<i32>()?;
            let picked = parse_bool(required_arg(&arguments, 4, "missing picked argument")?)?;
            let rejected = parse_bool(required_arg(&arguments, 5, "missing rejected argument")?)?;
            let color_label = arguments.get(6).cloned().unwrap_or_default();
            catalog
                .upsert_asset_review_state(&AssetReviewStateRecord {
                    asset_id: asset_id.to_string(),
                    rating,
                    picked,
                    rejected,
                    color_label,
                })
                .await?;
        }
        "set-relink-path" => {
            let asset_id = required_arg(&arguments, 2, "missing asset_id argument")?;
            let relinked_file_path =
                required_arg(&arguments, 3, "missing relinked_file_path argument")?;
            if relinked_file_path.trim().is_empty() {
                catalog.remove_asset_relink(asset_id).await?;
            } else {
                catalog
                    .set_asset_relink(asset_id, relinked_file_path)
                    .await?;
            }
        }
        "set-edit-recipe-v13" => {
            let asset_id = required_arg(&arguments, 2, "missing asset_id argument")?;
            let recipe_payload = required_arg(&arguments, 3, "missing recipe_payload argument")?;
            let cache_key = required_arg(&arguments, 4, "missing cache_key argument")?;
            let provenance_payload =
                required_arg(&arguments, 5, "missing provenance_payload argument")?;
            catalog
                .upsert_canonical_edit_recipe_v13(
                    asset_id,
                    recipe_payload,
                    cache_key,
                    provenance_payload,
                )
                .await?;
        }
        "set-recipe-migration" => {
            let asset_id = required_arg(&arguments, 2, "missing asset_id argument")?;
            let source_recipe_version =
                required_arg(&arguments, 3, "missing source_recipe_version argument")?
                    .parse::<i32>()?;
            let source_renderer = required_arg(&arguments, 4, "missing source_renderer argument")?;
            let target_renderer = required_arg(&arguments, 5, "missing target_renderer argument")?;
            let profile_identities: Vec<String> = serde_json::from_str(required_arg(
                &arguments,
                6,
                "missing profile_identities_json argument",
            )?)?;
            let plan_hash = required_arg(&arguments, 7, "missing plan_hash argument")?;
            let evidence_hash = required_arg(&arguments, 8, "missing evidence_hash argument")?;
            let approval_time = required_arg(&arguments, 9, "missing approval_time argument")?;
            let rollback_pointer =
                required_arg(&arguments, 10, "missing rollback_pointer argument")?;
            let status = required_arg(&arguments, 11, "missing status argument")?;
            catalog
                .upsert_recipe_migration_record(&RecipeMigrationRecord {
                    asset_id: asset_id.to_string(),
                    source_recipe_version,
                    source_renderer: source_renderer.to_string(),
                    target_renderer: target_renderer.to_string(),
                    profile_identities,
                    plan_hash: plan_hash.to_string(),
                    evidence_hash: evidence_hash.to_string(),
                    approval_time: (!approval_time.trim().is_empty())
                        .then(|| approval_time.to_string()),
                    rollback_pointer: rollback_pointer.to_string(),
                    status: status.to_string(),
                    created_at: String::new(),
                    updated_at: String::new(),
                })
                .await?;
        }
        _ => {
            eprintln!(
                "Usage: {} <create-project|add-asset-to-project|remove-asset-from-project|delete-asset|set-review-state|set-relink-path|set-edit-recipe-v13|set-recipe-migration> ...",
                arguments.first().map(String::as_str).unwrap_or("catalog_state")
            );
            std::process::exit(1);
        }
    }

    Ok(())
}

fn required_arg<'a>(
    arguments: &'a [String],
    index: usize,
    message: &str,
) -> Result<&'a str, Box<dyn std::error::Error>> {
    arguments
        .get(index)
        .map(String::as_str)
        .ok_or_else(|| message.into())
}

fn parse_bool(value: &str) -> Result<bool, Box<dyn std::error::Error>> {
    match value.trim().to_ascii_lowercase().as_str() {
        "1" | "true" | "yes" => Ok(true),
        "0" | "false" | "no" => Ok(false),
        _ => Err(format!("invalid bool value: {value}").into()),
    }
}
