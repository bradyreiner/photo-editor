use photo_catalog::{
    run_catalog_helper, AppRuntimePaths, CatalogRepository, CatalogRuntime, PersistedExportJob,
};

pub(crate) fn main() -> std::process::ExitCode {
    run_catalog_helper(run)
}

async fn run() -> Result<(), Box<dyn std::error::Error>> {
    let arguments = photo_catalog::helper_arguments::read_helper_arguments()?;
    let command = arguments.get(1).map(String::as_str).unwrap_or("list");

    let catalog = CatalogRuntime::open(&AppRuntimePaths::from_env()).await?;

    match command {
        "health" => {
            catalog.healthcheck().await?;
            println!("{{\"ready\":true}}");
        }
        "list" => {
            let jobs = catalog.list_export_jobs().await?;
            println!("{}", serde_json::to_string_pretty(&jobs)?);
        }
        "upsert" => {
            let payload = arguments.get(2).ok_or("missing export job payload")?;
            let job: PersistedExportJob = serde_json::from_str(payload)?;
            catalog.upsert_export_job(&job).await?;
        }
        "import-legacy" => {
            let payload = arguments.get(2).ok_or("missing legacy job payload")?;
            let job: PersistedExportJob = serde_json::from_str(payload)?;
            let accepted = catalog.import_legacy_export_job(&job).await?;
            println!(
                "{}",
                serde_json::json!({"accepted": accepted, "job_id": job.job_id})
            );
        }
        "delete" => {
            let job_id = arguments.get(2).ok_or("missing job_id argument")?;
            catalog.delete_export_job(job_id).await?;
        }
        _ => {
            eprintln!(
                "Usage: {} <health|list|upsert|import-legacy|delete> ...",
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
