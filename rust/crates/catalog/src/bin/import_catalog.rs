use std::env;
use std::path::PathBuf;

use photo_catalog::{
    discover_assets, CatalogService, ImportMode, ImportRequest, PostgresCatalogConfig,
};

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let arguments: Vec<String> = env::args().collect();
    if arguments.len() < 4 {
        print_usage(&arguments[0]);
        std::process::exit(1);
    }

    let mount_path = PathBuf::from(&arguments[1]);
    let drive_id = arguments[2].clone();
    let drive_name = arguments[3].clone();
    let import_mode = parse_import_mode(arguments.get(4).map(String::as_str));

    let request = ImportRequest {
        drive_id,
        drive_name,
        mount_path,
        import_mode,
    };

    let summary = discover_assets(&request)?;
    let config = PostgresCatalogConfig::from_env();
    let catalog = CatalogService::connect(&config).await?;
    catalog.initialize_schema().await?;
    catalog.import_summary(&summary).await?;

    println!(
        "Imported {} assets for drive '{}' (skipped {} unsupported files).",
        summary.assets.len(),
        summary.drive.display_name,
        summary.skipped_unsupported
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
        "Usage: {program_name} <mount_path> <drive_id> <drive_name> [referenced|managed]"
    );
}
