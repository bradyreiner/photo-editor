use photo_catalog::{
    run_catalog_helper, AppRuntimePaths, CatalogRepository, CatalogRuntime,
    PersistedTimelineDocument,
};

pub(crate) fn main() -> std::process::ExitCode {
    run_catalog_helper(run)
}

async fn run() -> Result<(), Box<dyn std::error::Error>> {
    let catalog = CatalogRuntime::open(&AppRuntimePaths::from_env()).await?;

    let arguments = photo_catalog::helper_arguments::read_helper_arguments()?;
    let command = arguments.get(1).map(String::as_str).unwrap_or("list");

    match command {
        "list" => {
            let documents = catalog.list_timeline_documents().await?;
            println!("{}", serde_json::to_string_pretty(&documents)?);
        }
        "get" => {
            let timeline_id = arguments.get(2).ok_or("missing timeline_id argument")?;
            let document = catalog.get_timeline_document(timeline_id).await?;
            println!("{}", serde_json::to_string_pretty(&document)?);
        }
        "upsert" => {
            let payload = arguments
                .get(2)
                .ok_or("missing timeline document payload")?;
            let document: PersistedTimelineDocument = serde_json::from_str(payload)?;
            catalog.upsert_timeline_document(&document).await?;
        }
        "replace" => {
            let payload = arguments
                .get(2)
                .ok_or("missing timeline document payload")?;
            let documents: Vec<PersistedTimelineDocument> = serde_json::from_str(payload)?;
            catalog.replace_timeline_documents(&documents).await?;
        }
        _ => {
            eprintln!(
                "Usage: {} <list|get|upsert|replace> ...",
                arguments
                    .first()
                    .map(String::as_str)
                    .unwrap_or("catalog_timeline_documents")
            );
            std::process::exit(1);
        }
    }

    Ok(())
}
