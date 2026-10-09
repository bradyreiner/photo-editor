use photo_catalog::{
    run_catalog_helper, AgentAssetRecipeSnapshot, AgentCanonicalRecipe, AppRuntimePaths,
    CatalogRepository, CatalogRuntime,
};
use serde_json::Value;

pub(crate) fn main() -> std::process::ExitCode {
    run_catalog_helper(run)
}

async fn run() -> Result<(), Box<dyn std::error::Error>> {
    let arguments = photo_catalog::helper_arguments::read_helper_arguments()?;
    let (owned_snapshot, argument_start) = parse_owned_snapshot(&arguments)?;
    if arguments.get(argument_start).map(String::as_str) == Some("--legacy-v1") {
        if arguments.len() != argument_start + 3 {
            return Err("legacy save requires asset id and recipe JSON".into());
        }
        let asset_id = &arguments[argument_start + 1];
        let recipe_json = &arguments[argument_start + 2];
        let recipe = parse_legacy_recipe(asset_id, recipe_json)?;
        let catalog = CatalogRuntime::open(&AppRuntimePaths::from_env()).await?;
        if let Some(expected) = owned_snapshot.as_ref() {
            catalog
                .save_agent_edit_recipe_if_unchanged(asset_id, expected, &recipe, None)
                .await?;
        } else {
            catalog.upsert_edit_recipe(asset_id, &recipe).await?;
        }
        return Ok(());
    }
    if arguments.len() != argument_start + 5 {
        return Err("save_edit_recipe requires five recipe arguments".into());
    }

    let asset_id = &arguments[argument_start];
    let recipe_json = &arguments[argument_start + 1];
    let recipe_value: Value = serde_json::from_str(recipe_json)?;
    let canonical = AgentCanonicalRecipe {
        recipe_payload: arguments[argument_start + 2].clone(),
        cache_key: arguments[argument_start + 3].clone(),
        provenance_payload: arguments[argument_start + 4].clone(),
    };

    let catalog = CatalogRuntime::open(&AppRuntimePaths::from_env()).await?;
    if let Some(expected) = owned_snapshot.as_ref() {
        catalog
            .save_agent_edit_recipe_if_unchanged(
                asset_id,
                expected,
                &recipe_value,
                Some(&canonical),
            )
            .await?;
    } else {
        catalog
            .upsert_edit_recipe_compatibility_pair(
                asset_id,
                &recipe_value,
                &canonical.recipe_payload,
                &canonical.cache_key,
                &canonical.provenance_payload,
            )
            .await?;
    }

    Ok(())
}

fn parse_owned_snapshot(
    arguments: &[String],
) -> Result<(Option<AgentAssetRecipeSnapshot>, usize), Box<dyn std::error::Error>> {
    if arguments.get(1).map(String::as_str) != Some("--agent-owned-v1") {
        return Ok((None, 1));
    }
    let snapshot_json = arguments
        .get(2)
        .ok_or("owned save is missing its expected recipe snapshot")?;
    let snapshot = serde_json::from_str(snapshot_json)?;
    Ok((Some(snapshot), 3))
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
    fn owned_save_requires_a_typed_snapshot_prefix() {
        let snapshot = serde_json::json!({
            "schema": "agent-asset-recipe:v1",
            "state": "present",
            "asset_id": "asset",
            "source_path": "/library/photo.png",
            "source_sha256": "a".repeat(64),
            "legacy_recipe": null,
            "canonical_recipe": null,
            "catalog_row_state": {"rating": 0}
        });
        let arguments = vec![
            "save_edit_recipe".to_owned(),
            "--agent-owned-v1".to_owned(),
            snapshot.to_string(),
            "asset".to_owned(),
            "{}".to_owned(),
            "{}".to_owned(),
            "cache".to_owned(),
            "{}".to_owned(),
        ];
        let (expected, start) = parse_owned_snapshot(&arguments).unwrap();
        assert_eq!(start, 3);
        assert_eq!(expected.unwrap().asset_id, "asset");

        let mut malformed = arguments;
        malformed[2] = r#"{"schema":"agent-asset-recipe:v1","unexpected":true}"#.into();
        assert!(parse_owned_snapshot(&malformed).is_err());
    }

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
