use std::collections::BTreeMap;
use std::env;

use photo_catalog::{
    AppRuntimePaths, AssetReviewStateRecord, CatalogAssetSnapshot, CatalogService, DriveRecord,
    PersistedProject, PostgresCatalogConfig,
};
use serde::Serialize;

#[derive(Serialize)]
struct Snapshot {
    assets: Vec<CatalogAssetSnapshot>,
    drives: Vec<DriveRecord>,
    projects: Vec<PersistedProject>,
    review_states: BTreeMap<String, ReviewState>,
    relinked_paths: BTreeMap<String, String>,
}

#[derive(Serialize)]
struct ReviewState {
    rating: i32,
    picked: bool,
    rejected: bool,
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let arguments: Vec<String> = env::args().collect();
    let limit = parse_limit(arguments.get(1).map(String::as_str));

    AppRuntimePaths::from_env().ensure_directories()?;
    let config = PostgresCatalogConfig::from_env();
    let catalog = CatalogService::connect(&config).await?;
    catalog.initialize_schema().await?;

    let snapshot = Snapshot {
        assets: catalog.list_asset_snapshots(limit).await?,
        drives: catalog.list_drives().await?,
        projects: catalog.list_projects().await?,
        review_states: review_state_map(catalog.list_asset_review_states().await?),
        relinked_paths: catalog
            .list_asset_relinks()
            .await?
            .into_iter()
            .map(|record| (record.asset_id, record.relinked_file_path))
            .collect(),
    };

    println!("{}", serde_json::to_string_pretty(&snapshot)?);

    Ok(())
}

fn parse_limit(argument: Option<&str>) -> i64 {
    match argument.and_then(|value| value.parse::<i64>().ok()) {
        Some(limit) if limit > 0 => limit,
        _ => 0,
    }
}

fn review_state_map(records: Vec<AssetReviewStateRecord>) -> BTreeMap<String, ReviewState> {
    records
        .into_iter()
        .map(|record| {
            (
                record.asset_id,
                ReviewState {
                    rating: record.rating,
                    picked: record.picked,
                    rejected: record.rejected,
                },
            )
        })
        .collect()
}
