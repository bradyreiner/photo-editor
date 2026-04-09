use std::env;

use photo_catalog::{
    AppRuntimePaths, AssetReviewStateRecord, CatalogService, PostgresCatalogConfig,
};
use serde_json::Value;

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let arguments: Vec<String> = env::args().collect();
    let command = arguments.get(1).map(String::as_str).unwrap_or("");

    AppRuntimePaths::from_env().ensure_directories()?;
    let config = PostgresCatalogConfig::from_env();
    let catalog = CatalogService::connect(&config).await?;
    catalog.initialize_schema().await?;

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
            catalog
                .upsert_asset_review_state(&AssetReviewStateRecord {
                    asset_id: asset_id.to_string(),
                    rating,
                    picked,
                    rejected,
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
        "set-edit-recipe" => {
            let asset_id = required_arg(&arguments, 2, "missing asset_id argument")?;
            let recipe_json = required_arg(&arguments, 3, "missing recipe_json argument")?;
            let recipe_value: Value = serde_json::from_str(recipe_json)?;
            catalog.upsert_edit_recipe(asset_id, &recipe_value).await?;
        }
        _ => {
            eprintln!(
                "Usage: {} <create-project|add-asset-to-project|remove-asset-from-project|delete-asset|set-review-state|set-relink-path|set-edit-recipe> ...",
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
