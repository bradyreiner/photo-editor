use std::env;

use photo_catalog::{
    run_catalog_helper, AgentAssetRecipeSnapshot, AgentCatalogRowSnapshot, AppRuntimePaths,
    AssetReviewStateRecord, CatalogRepository, CatalogRuntime, RecipeMigrationRecord,
};

pub(crate) fn main() -> std::process::ExitCode {
    run_catalog_helper(run)
}

async fn run() -> Result<(), Box<dyn std::error::Error>> {
    let arguments: Vec<String> = env::args().collect();
    let command = arguments.get(1).map(String::as_str).unwrap_or("");

    let catalog = CatalogRuntime::open(&AppRuntimePaths::from_env()).await?;

    match command {
        "set-asset-metadata" => {
            #[derive(serde::Deserialize)]
            #[serde(deny_unknown_fields)]
            struct Changes { records: Vec<photo_catalog::AssetMetadataRecord> }
            let path = required_arg(&arguments, 2, "missing metadata payload path")?;
            let changes: Changes = serde_json::from_slice(&std::fs::read(path)?)?;
            let result = catalog.upsert_asset_metadata(&changes.records).await?;
            println!("{}", serde_json::to_string(&result)?);
        }
        "update-project-memberships" => {
            #[derive(serde::Deserialize)]
            #[serde(deny_unknown_fields)]
            struct MembershipChanges {
                project_name: String,
                added: Vec<String>,
                removed: Vec<String>,
            }
            let path = required_arg(&arguments, 2, "missing membership payload path")?;
            let changes: MembershipChanges = serde_json::from_slice(&std::fs::read(path)?)?;
            let removed: std::collections::HashSet<_> = changes.removed.iter().collect();
            if changes.project_name.trim().is_empty()
                || changes
                    .added
                    .iter()
                    .chain(&changes.removed)
                    .any(|id| id.trim().is_empty())
                || changes.added.iter().any(|id| removed.contains(id))
            {
                return Err("invalid project membership payload".into());
            }
            let missing = catalog
                .update_project_memberships(&changes.project_name, &changes.added, &changes.removed)
                .await?;
            if !missing.is_empty() {
                return Err(format!(
                    "{} project memberships remain local because their assets are not cataloged",
                    missing.len()
                )
                .into());
            }
        }
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
        "agent-asset-recipe-snapshot" => {
            #[derive(serde::Deserialize)]
            #[serde(deny_unknown_fields, rename_all = "camelCase")]
            struct Request { asset_id: String, source_path: String }
            let request_path = required_arg(&arguments, 2, "missing request payload path")?;
            let result_path = required_arg(&arguments, 3, "missing snapshot result path")?;
            let request: Request = serde_json::from_slice(&std::fs::read(request_path)?)?;
            let snapshot = catalog
                .agent_asset_recipe_snapshot(&request.asset_id, &request.source_path)
                .await?;
            std::fs::write(result_path, serde_json::to_vec(&snapshot)?)?;
        }
        "restore-agent-asset-recipe" => {
            #[derive(serde::Deserialize)]
            #[serde(deny_unknown_fields, rename_all = "camelCase")]
            struct Request {
                expected: AgentAssetRecipeSnapshot,
                before: AgentAssetRecipeSnapshot,
            }
            let request_path = required_arg(&arguments, 2, "missing recipe restore payload path")?;
            let request: Request = serde_json::from_slice(&std::fs::read(request_path)?)?;
            catalog
                .restore_agent_asset_recipe(&request.expected, &request.before)
                .await?;
        }
        "agent-catalog-row-snapshot" => {
            #[derive(serde::Deserialize)]
            #[serde(deny_unknown_fields, rename_all = "camelCase")]
            struct Request { source_path: String }
            let request_path = required_arg(&arguments, 2, "missing request payload path")?;
            let result_path = required_arg(&arguments, 3, "missing snapshot result path")?;
            let request: Request = serde_json::from_slice(&std::fs::read(request_path)?)?;
            let snapshot = catalog.agent_catalog_row_snapshot(&request.source_path).await?;
            std::fs::write(result_path, serde_json::to_vec(&snapshot)?)?;
        }
        "remove-agent-catalog-row" => {
            let request_path = required_arg(&arguments, 2, "missing catalog row receipt path")?;
            let expected: AgentCatalogRowSnapshot = serde_json::from_slice(&std::fs::read(request_path)?)?;
            catalog.remove_agent_catalog_row_if_unchanged(&expected).await?;
        }
        "set-review-states" => {
            #[derive(serde::Deserialize)]
            #[serde(deny_unknown_fields)]
            struct ReviewChanges { states: Vec<AssetReviewStateRecord> }
            let path = required_arg(&arguments, 2, "missing review payload path")?;
            let changes: ReviewChanges = serde_json::from_slice(&std::fs::read(path)?)?;
            let result = catalog.upsert_asset_review_states(&changes.states).await?;
            println!("{}", serde_json::to_string(&result)?);
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
                "Usage: {} <create-project|update-project-memberships|add-asset-to-project|remove-asset-from-project|delete-asset|set-review-state|set-review-states|set-relink-path|set-edit-recipe-v13|set-recipe-migration> ...",
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
