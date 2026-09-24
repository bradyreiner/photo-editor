use photo_catalog::{run_catalog_helper, AppRuntimePaths, CatalogRepository, CatalogRuntime};
use serde_json::Value;

pub(crate) fn main() -> std::process::ExitCode {
    run_catalog_helper(run)
}

async fn run() -> Result<(), Box<dyn std::error::Error>> {
    let arguments = photo_catalog::helper_arguments::read_helper_arguments()?;
    if arguments.get(1).map(String::as_str) == Some("--legacy-v1") {
        if arguments.len() != 4 {
            return Err("legacy save requires asset id and recipe JSON".into());
        }
        let recipe = parse_legacy_recipe(&arguments[2], &arguments[3])?;
        let catalog = CatalogRuntime::open(&AppRuntimePaths::from_env()).await?;
        catalog.upsert_edit_recipe(&arguments[2], &recipe).await?;
        return Ok(());
    }
    if arguments.len() != 6 {
        return Err("save_edit_recipe requires five recipe arguments".into());
    }

    let asset_id = arguments.get(1).ok_or("missing asset_id argument")?;
    let recipe_json = arguments.get(2).ok_or("missing recipe_json argument")?;
    let recipe_value: Value = serde_json::from_str(recipe_json)?;
    let canonical_payload = arguments
        .get(3)
        .ok_or("missing canonical_recipe_payload argument")?;
    let canonical_cache_key = arguments
        .get(4)
        .ok_or("missing canonical_cache_key argument")?;
    let canonical_provenance_payload = arguments
        .get(5)
        .ok_or("missing canonical_provenance_payload argument")?;

    let catalog = CatalogRuntime::open(&AppRuntimePaths::from_env()).await?;
    catalog
        .upsert_edit_recipe_compatibility_pair(
            asset_id,
            &recipe_value,
            canonical_payload,
            canonical_cache_key,
            canonical_provenance_payload,
        )
        .await?;

    Ok(())
}

fn parse_legacy_recipe(asset_id: &str, payload: &str) -> Result<Value, Box<dyn std::error::Error>> {
    let recipe: Value = serde_json::from_str(payload)?;
    if asset_id.trim().is_empty()
        || recipe
            .pointer("/colorPipeline/photographicRenderingModel")
            .and_then(Value::as_str)
            != Some("legacy-oklch-v1")
        || recipe
            .get("recipeVersion")
            .and_then(Value::as_u64)
            .is_none()
    {
        return Err("legacy save requires an explicit versioned legacy renderer recipe".into());
    }
    Ok(recipe)
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn legacy_save_preserves_payload_and_rejects_other_renderers() {
        let recipe = serde_json::json!({"recipeVersion": 12,
            "colorPipeline": {"photographicRenderingModel": "legacy-oklch-v1"},
            "curves": {"points": [[0.0, 0.1], [1.0, 0.9]]}, "maskData": "exact-data"});
        assert_eq!(
            parse_legacy_recipe("asset", &recipe.to_string()).unwrap(),
            recipe
        );
        assert!(parse_legacy_recipe(" ", &recipe.to_string()).is_err());
        for model in ["aces2-ocio-v1", "unknown", ""] {
            let mut changed = recipe.clone();
            changed["colorPipeline"]["photographicRenderingModel"] = model.into();
            assert!(parse_legacy_recipe("asset", &changed.to_string()).is_err());
        }
        assert!(parse_legacy_recipe("asset", "{}").is_err());
        assert!(parse_legacy_recipe("asset", "invalid").is_err());
    }
}
