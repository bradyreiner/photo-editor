use std::time::Instant;

use photo_catalog::{
    observability::Logger, run_catalog_helper, AppRuntimePaths, CatalogService,
    PostgresCatalogConfig,
};

pub(crate) fn main() -> std::process::ExitCode {
    run_catalog_helper(run)
}

async fn run() -> Result<(), Box<dyn std::error::Error + Send + Sync>> {
    let logger = Logger::from_env("catalog_migrate")?;
    let started_at = Instant::now();
    let runtime_paths = AppRuntimePaths::from_env();
    runtime_paths.ensure_directories()?;

    let config = PostgresCatalogConfig::from_env();
    let catalog = CatalogService::connect(&config).await?;
    catalog.initialize_schema().await?;
    catalog.healthcheck().await?;

    println!(
        "Catalog migrations applied successfully.\nData: {}\nLogs: {}\nBackups: {}",
        runtime_paths.app_data_dir.display(),
        runtime_paths.log_dir.display(),
        runtime_paths.backup_dir.display()
    );
    logger.info(
        "catalog_migrate.completed",
        serde_json::json!({
            "app_data_dir": runtime_paths.app_data_dir.display().to_string(),
            "log_dir": runtime_paths.log_dir.display().to_string(),
            "backup_dir": runtime_paths.backup_dir.display().to_string(),
            "elapsed_ms": started_at.elapsed().as_millis() as u64,
        }),
    );

    Ok(())
}
