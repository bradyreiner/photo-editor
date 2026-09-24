use std::env;

use photo_catalog::{
    run_catalog_helper, AppRuntimePaths, CatalogRepository, CatalogRuntime,
    PersistedCarouselDocument,
};

pub(crate) fn main() -> std::process::ExitCode {
    run_catalog_helper(run)
}

async fn run() -> Result<(), Box<dyn std::error::Error>> {
    let arguments: Vec<String> = env::args().collect();
    let command = arguments.get(1).map(String::as_str).unwrap_or("list");

    let catalog = CatalogRuntime::open(&AppRuntimePaths::from_env()).await?;

    match command {
        "list" => {
            let documents = catalog.list_carousel_documents().await?;
            println!("{}", serde_json::to_string_pretty(&documents)?);
        }
        "get" => {
            let document_id = arguments.get(2).ok_or("missing document_id argument")?;
            let document = catalog.get_carousel_document(document_id).await?;
            println!("{}", serde_json::to_string_pretty(&document)?);
        }
        "upsert" => {
            let payload = arguments
                .get(2)
                .ok_or("missing carousel document payload")?;
            let document: PersistedCarouselDocument = serde_json::from_str(payload)?;
            catalog.upsert_carousel_document(&document).await?;
        }
        _ => {
            eprintln!(
                "Usage: {} <list|get|upsert> ...",
                arguments
                    .first()
                    .map(String::as_str)
                    .unwrap_or("catalog_carousel_documents")
            );
            std::process::exit(1);
        }
    }

    Ok(())
}
