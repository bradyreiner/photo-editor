use std::env;
use std::fs;
use std::path::PathBuf;
use std::time::Instant;

use photo_catalog::{
    discover_assets, observability::Logger, run_catalog_helper, AppRuntimePaths, CatalogRepository,
    CatalogRuntime, DuplicatePolicy, ImportMode, ImportRequest, MANAGED_MEDIA_DIR_ENV,
    ORIGINALS_BACKUP_DIR_ENV,
};

pub(crate) fn main() -> std::process::ExitCode {
    run_catalog_helper(run)
}

async fn run() -> Result<(), Box<dyn std::error::Error + Send + Sync>> {
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
    let managed_root = optional_path(arguments.get(6).map(String::as_str))
        .or_else(|| env::var_os(MANAGED_MEDIA_DIR_ENV).map(PathBuf::from))
        .unwrap_or_else(|| runtime_paths.app_data_dir.join("managed"));
    let backup_root = optional_path(arguments.get(7).map(String::as_str))
        .or_else(|| env::var_os(ORIGINALS_BACKUP_DIR_ENV).map(PathBuf::from));
    let organization_template = arguments
        .get(8)
        .filter(|value| !value.trim().is_empty() && value.trim() != "-")
        .cloned()
        .unwrap_or_else(|| "source".to_string());
    let raw_jpeg_policy = arguments
        .get(9)
        .filter(|value| !value.trim().is_empty() && value.trim() != "-")
        .cloned()
        .unwrap_or_else(|| "both".to_string());
    let duplicate_policy =
        DuplicatePolicy::parse(arguments.get(10).map(String::as_str).unwrap_or("skip"))?;
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
        managed_root: Some(managed_root),
        backup_root: backup_root.clone(),
        organization_template: organization_template.clone(),
        raw_jpeg_policy: raw_jpeg_policy.clone(),
        duplicate_policy,
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
            "reused_existing": summary.reused_existing,
            "backed_up": summary.backed_up,
            "reused_backup": summary.reused_backup,
            "backup_root": backup_root.as_ref().map(|path| path.display().to_string()),
            "verification": "sha256",
            "organization_template": organization_template,
            "raw_jpeg_policy": raw_jpeg_policy,
            "duplicate_policy": duplicate_policy.as_str(),
            "skipped_policy": summary.skipped_policy,
            "skipped_duplicates": summary.skipped_duplicates,
            "elapsed_ms": discovery_elapsed_ms,
        }),
    );
    let catalog = CatalogRuntime::open(&runtime_paths).await?;
    let database_started_at = Instant::now();
    catalog.import_summary(&summary).await?;
    let database_elapsed_ms = database_started_at.elapsed().as_millis() as u64;

    println!(
        "Imported {} assets for drive '{}' (skipped {} unsupported files).",
        summary.assets.len(),
        summary.drive.display_name,
        summary.skipped_unsupported
    );
    println!(
        "VIREL_IMPORT_RESULT={}",
        serde_json::to_string(&serde_json::json!({
            "schema": "import-result:v1",
            "importedCount": summary.assets.len(),
            "skippedUnsupported": summary.skipped_unsupported,
            "resumedCount": summary.reused_existing,
            "backedUpCount": summary.backed_up,
            "reusedBackupCount": summary.reused_backup,
            "backupDestination": backup_root.as_ref().map(|path| path.display().to_string()),
            "verificationAlgorithm": "SHA-256",
            "failureCount": 0,
            "skippedPolicyCount": summary.skipped_policy,
            "skippedDuplicateCount": summary.skipped_duplicates,
        }))?
    );
    logger.info(
        "catalog_import.completed",
        serde_json::json!({
            "drive_id": drive_id,
            "drive_name": drive_name,
            "imported_assets": summary.assets.len(),
            "skipped_unsupported": summary.skipped_unsupported,
            "reused_existing": summary.reused_existing,
            "backed_up": summary.backed_up,
            "reused_backup": summary.reused_backup,
            "skipped_duplicates": summary.skipped_duplicates,
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
        "Usage: {program_name} <mount_path> <drive_id> <drive_name> [referenced|managed] [selection_manifest|-] [managed_root|-] [backup_root|-] [source|date-camera] [both|raw-only|jpeg-only] [skip|reuse-identical|replace]"
    );
}

fn optional_path(value: Option<&str>) -> Option<PathBuf> {
    value
        .map(str::trim)
        .filter(|value| !value.is_empty() && *value != "-")
        .map(PathBuf::from)
}

fn load_selected_paths(
    path: Option<&str>,
) -> Result<Vec<PathBuf>, Box<dyn std::error::Error + Send + Sync>> {
    let Some(path) = path.filter(|value| !value.trim().is_empty() && value.trim() != "-") else {
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
