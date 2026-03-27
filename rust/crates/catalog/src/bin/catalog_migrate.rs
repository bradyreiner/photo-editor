use photo_catalog::{AppRuntimePaths, CatalogService, PostgresCatalogConfig};

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
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

    Ok(())
}
