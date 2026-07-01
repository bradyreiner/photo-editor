use std::env;
use std::fs;
use std::path::PathBuf;
use std::time::Instant;

use photo_catalog::{
    discover_assets, observability::Logger, AppRuntimePaths, CatalogService, ImportMode,
    ImportRequest, PostgresCatalogConfig,
};

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error + Send + Sync>> {
    let logger = Logger::from_env("import_catalog")?;
    let started_at = Instant::now();
    let arguments: Vec<String> = env::args().collect();
    if arguments.len() < 4 {
        print_usage(&arguments[0]);
        std::process::exit(1);
    }

    let mount_path = PathBuf::from(&arguments[1]);
    let drive_id = arguments[2].clone();
    let drive_name = arguments[3].clone();
    let import_mode = parse_import_mode(arguments.get(4).map(String::as_str));
    let selected_paths = load_selected_paths(arguments.get(5).map(String::as_str))?;
    let runtime_paths = AppRuntimePaths::from_env();
    runtime_paths.ensure_directories()?;
    logger.info(
        "catalog_import.started",
        serde_json::json!({
            "mount_path": mount_path.display().to_string(),
            "drive_id": &drive_id,
            "selected_path_count": selected_paths.len(),
        }),
    );

    let request = ImportRequest {
        drive_id: drive_id.clone(),
        drive_name: drive_name.clone(),
        mount_path,
        import_mode,
        selected_paths,
        managed_root: Some(runtime_paths.app_data_dir.join("managed")),
    };

    let discovery_started_at = Instant::now();
    let summary = discover_assets(&request)?;
    let discovery_elapsed_ms = discovery_started_at.elapsed().as_millis() as u64;
    logger.info(
        "catalog_import.discovery.completed",
        serde_json::json!({
            "drive_id": &drive_id,
            "candidate_assets": summary.assets.len(),
            "skipped_unsupported": summary.skipped_unsupported,
            "elapsed_ms": discovery_elapsed_ms,
        }),
    );
    let config = PostgresCatalogConfig::from_env();
    let catalog = CatalogService::connect(&config).await?;
    catalog.initialize_schema().await?;
    let database_started_at = Instant::now();
    catalog.import_summary(&summary).await?;
    let database_elapsed_ms = database_started_at.elapsed().as_millis() as u64;

    println!(
        "Imported {} assets for drive '{}' (skipped {} unsupported files).",
        summary.assets.len(),
        summary.drive.display_name,
        summary.skipped_unsupported
    );
    logger.info(
        "catalog_import.completed",
        serde_json::json!({
            "drive_id": drive_id,
            "drive_name": drive_name,
            "imported_assets": summary.assets.len(),
            "skipped_unsupported": summary.skipped_unsupported,
            "discovery_elapsed_ms": discovery_elapsed_ms,
            "database_elapsed_ms": database_elapsed_ms,
            "elapsed_ms": started_at.elapsed().as_millis() as u64,
        }),
    );

    Ok(())
}

fn parse_import_mode(value: Option<&str>) -> ImportMode {
    match value.unwrap_or("referenced").to_ascii_lowercase().as_str() {
        "managed" => ImportMode::Managed,
        _ => ImportMode::Referenced,
    }
}

fn print_usage(program_name: &str) {
    eprintln!(
        "Usage: {program_name} <mount_path> <drive_id> <drive_name> [referenced|managed] [selection_manifest]"
    );
}

fn load_selected_paths(
    path: Option<&str>,
) -> Result<Vec<PathBuf>, Box<dyn std::error::Error + Send + Sync>> {
    let Some(path) = path else {
        return Ok(Vec::new());
    };

    let contents = fs::read_to_string(path)?;
    Ok(contents
        .lines()
        .map(str::trim)
        .filter(|line| !line.is_empty())
        .map(PathBuf::from)
        .collect())
}
