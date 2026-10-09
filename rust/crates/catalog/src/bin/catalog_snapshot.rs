use std::collections::BTreeMap;
use std::env;
use std::io::{self, BufWriter, Write};

use photo_catalog::{
    run_catalog_helper, AppRuntimePaths, AssetReviewStateRecord, CatalogRepository, CatalogRuntime,
};
use serde::Serialize;

#[derive(Serialize)]
struct ReviewState {
    rating: i32,
    picked: bool,
    rejected: bool,
    color_label: String,
}

pub(crate) fn main() -> std::process::ExitCode {
    run_catalog_helper(run)
}

async fn run() -> Result<(), Box<dyn std::error::Error>> {
    let arguments: Vec<String> = env::args().collect();
    let limit = parse_limit(arguments.get(1).map(String::as_str));

    let catalog = CatalogRuntime::open(&AppRuntimePaths::from_env()).await?;

    let stdout = io::stdout();
    let mut output = BufWriter::with_capacity(64 * 1024, stdout.lock());
    output.write_all(b"{\"assets\":")?;
    catalog.write_asset_snapshots_json(limit, &mut output).await?;
    output.write_all(b",\"drives\":")?;
    serde_json::to_writer(&mut output, &catalog.list_drives().await?)?;
    output.write_all(b",\"projects\":")?;
    serde_json::to_writer(&mut output, &catalog.list_projects().await?)?;
    output.write_all(b",\"review_states\":")?;
    serde_json::to_writer(
        &mut output,
        &review_state_map(catalog.list_asset_review_states().await?),
    )?;
    output.write_all(b",\"asset_metadata\":")?;
    serde_json::to_writer(&mut output, &catalog.list_asset_metadata().await?)?;
    output.write_all(b",\"relinked_paths\":")?;
    let relinked_paths: BTreeMap<_, _> = catalog
        .list_asset_relinks()
        .await?
        .into_iter()
        .map(|record| (record.asset_id, record.relinked_file_path))
        .collect();
    serde_json::to_writer(&mut output, &relinked_paths)?;
    output.write_all(b"}\n")?;
    // A broken pipe or final flush failure must produce a nonzero helper exit;
    // consumers publish a snapshot only after successful process completion.
    output.flush()?;

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
                    color_label: record.color_label,
                },
            )
        })
        .collect()
}
