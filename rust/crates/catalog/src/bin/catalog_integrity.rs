use std::env;

use photo_catalog::{run_catalog_helper, AppRuntimePaths, CatalogRepository, CatalogRuntime};

pub(crate) fn main() -> std::process::ExitCode {
    run_catalog_helper(run)
}

async fn run() -> Result<(), Box<dyn std::error::Error>> {
    let require_clean = env::args()
        .skip(1)
        .any(|argument| argument == "--require-clean");
    let catalog = CatalogRuntime::open(&AppRuntimePaths::from_env()).await?;
    let report = catalog.catalog_integrity_report().await?;
    let finding_count = report.findings.len();
    println!("{}", serde_json::to_string_pretty(&report)?);
    if require_clean && finding_count > 0 {
        return Err(format!(
            "catalog integrity validation found {finding_count} violation(s); repair the identified records or restore a verified backup"
        )
        .into());
    }
    Ok(())
}
