use std::env;

use photo_catalog::{CatalogAssetSnapshot, CatalogService, DriveRecord, PostgresCatalogConfig};
use serde::Serialize;

#[derive(Serialize)]
struct Snapshot {
    assets: Vec<CatalogAssetSnapshot>,
    drives: Vec<DriveRecord>,
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let arguments: Vec<String> = env::args().collect();
    let limit = parse_limit(arguments.get(1).map(String::as_str));

    let config = PostgresCatalogConfig::from_env();
    let catalog = CatalogService::connect(&config).await?;
    catalog.initialize_schema().await?;

    let snapshot = Snapshot {
        assets: catalog.list_asset_snapshots(limit).await?,
        drives: catalog.list_drives().await?,
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
