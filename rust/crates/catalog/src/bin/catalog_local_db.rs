use std::env;
use std::time::Instant;

use photo_catalog::{observability::Logger, LocalCatalogDatabaseManager};

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error + Send + Sync>> {
    let logger = Logger::from_env("catalog_local_db")?;
    let started_at = Instant::now();
    let arguments: Vec<String> = env::args().collect();
    let command = arguments.get(1).map(String::as_str).unwrap_or("help");
    let manager = LocalCatalogDatabaseManager::from_env();
    logger.info(
        "catalog_local_db.started",
        serde_json::json!({
            "command": command,
            "run_directory": logger.run_directory().display().to_string(),
        }),
    );

    let result = match command {
        "ensure-running" => {
            let database_url = manager.ensure_ready_database_url().await?;
            println!(
                "Catalog database ready at {}",
                redact_connection_url(&database_url)
            );
        }
        "connection-url" => {
            let ensure_running = arguments
                .iter()
                .any(|argument| argument == "--ensure-running");
            if ensure_running {
                println!("{}", manager.ensure_ready_database_url().await?);
            } else if let Ok(database_url) = env::var("PHOTO_EDITOR_DATABASE_URL") {
                println!("{database_url}");
            } else {
                return Err(
                    "local database URL is unavailable until ensure-running has completed".into(),
                );
            }
        }
        "status" => {
            let status = manager.status().await?;
            println!("{}", serde_json::to_string_pretty(&status)?);
        }
        "stop" => {
            manager.stop()?;
            println!("Catalog database stopped.");
        }
        _ => print_usage(&arguments[0]),
    };

    logger.info(
        "catalog_local_db.completed",
        serde_json::json!({
            "command": command,
            "elapsed_ms": started_at.elapsed().as_millis() as u64,
        }),
    );

    Ok(result)
}

fn print_usage(program_name: &str) {
    eprintln!(
        "Usage: {program_name} <ensure-running|connection-url [--ensure-running]|status|stop>"
    );
}

fn redact_connection_url(database_url: &str) -> String {
    match database_url.split_once("://") {
        Some((scheme, rest)) => match rest.split_once('@') {
            Some((userinfo, host_part)) => {
                let user = userinfo
                    .split_once(':')
                    .map(|(name, _)| name)
                    .unwrap_or(userinfo);
                format!("{scheme}://{user}:<redacted>@{host_part}")
            }
            None => database_url.to_string(),
        },
        None => database_url.to_string(),
    }
}
