use std::env;

use photo_catalog::{AppRuntimePaths, CatalogService, PersistedExportJob, PostgresCatalogConfig};

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let arguments: Vec<String> = env::args().collect();
    let command = arguments.get(1).map(String::as_str).unwrap_or("list");

    AppRuntimePaths::from_env().ensure_directories()?;
    let config = PostgresCatalogConfig::from_env();
    let catalog = CatalogService::connect(&config).await?;
    catalog.initialize_schema().await?;

    match command {
        "list" => {
            let jobs = catalog.list_export_jobs().await?;
            println!("{}", serde_json::to_string_pretty(&jobs)?);
        }
        "upsert" => {
            let payload = arguments.get(2).ok_or("missing export job payload")?;
            let job: PersistedExportJob = serde_json::from_str(payload)?;
            catalog.upsert_export_job(&job).await?;
        }
        "delete" => {
            let job_id = arguments.get(2).ok_or("missing job_id argument")?;
            catalog.delete_export_job(job_id).await?;
        }
        _ => {
            eprintln!(
                "Usage: {} <list|upsert|delete> ...",
                arguments
                    .first()
                    .map(String::as_str)
                    .unwrap_or("catalog_export_jobs")
            );
            std::process::exit(1);
        }
    }

    Ok(())
}
