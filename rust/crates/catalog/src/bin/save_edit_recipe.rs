use std::env;

use photo_catalog::{AppRuntimePaths, CatalogService, PostgresCatalogConfig};
use serde_json::Value;

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let arguments: Vec<String> = env::args().collect();

    let asset_id = arguments.get(1).ok_or("missing asset_id argument")?;
    let recipe_json = arguments.get(2).ok_or("missing recipe_json argument")?;
    let recipe_value: Value = serde_json::from_str(recipe_json)?;

    AppRuntimePaths::from_env().ensure_directories()?;
    let config = PostgresCatalogConfig::from_env();
    let catalog = CatalogService::connect(&config).await?;
    catalog.initialize_schema().await?;
    catalog.upsert_edit_recipe(asset_id, &recipe_value).await?;

    Ok(())
}
