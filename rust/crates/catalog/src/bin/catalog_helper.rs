//! macOS release multicall entry point for the catalog helper fleet.
//!
//! The package installs this binary once and exposes the established helper
//! names as symlinks. Dispatching from argv[0] preserves every helper's command
//! line contract while avoiding thirteen separately linked copies of the same
//! Rust runtime and catalog dependency graph.

#[path = "accept_recipe_migration.rs"]
mod accept_recipe_migration;
#[path = "catalog_carousel_documents.rs"]
mod catalog_carousel_documents;
#[path = "catalog_export_jobs.rs"]
mod catalog_export_jobs;
#[path = "catalog_integrity.rs"]
mod catalog_integrity;
#[path = "catalog_local_db.rs"]
mod catalog_local_db;
#[path = "catalog_migrate.rs"]
mod catalog_migrate;
#[path = "catalog_migration_inventory.rs"]
mod catalog_migration_inventory;
#[path = "catalog_move_library.rs"]
mod catalog_move_library;
#[path = "catalog_snapshot.rs"]
mod catalog_snapshot;
#[path = "catalog_state.rs"]
mod catalog_state;
#[path = "catalog_timeline_documents.rs"]
mod catalog_timeline_documents;
#[path = "import_catalog.rs"]
mod import_catalog;
#[path = "save_edit_recipe.rs"]
mod save_edit_recipe;

use std::path::Path;
use std::process::ExitCode;

fn main() -> ExitCode {
    let invoked_path = std::env::args_os().next().unwrap_or_default();
    let invoked_name = Path::new(&invoked_path)
        .file_stem()
        .and_then(|name| name.to_str())
        .unwrap_or_default();

    match invoked_name {
        "accept_recipe_migration" => accept_recipe_migration::main(),
        "catalog_carousel_documents" => catalog_carousel_documents::main(),
        "catalog_export_jobs" => catalog_export_jobs::main(),
        "catalog_integrity" => catalog_integrity::main(),
        "catalog_local_db" => catalog_local_db::main(),
        "catalog_migrate" => catalog_migrate::main(),
        "catalog_migration_inventory" => catalog_migration_inventory::main(),
        "catalog_move_library" => catalog_move_library::main(),
        "catalog_snapshot" => catalog_snapshot::main(),
        "catalog_state" => catalog_state::main(),
        "catalog_timeline_documents" => catalog_timeline_documents::main(),
        "import_catalog" => import_catalog::main(),
        "save_edit_recipe" => save_edit_recipe::main(),
        _ => {
            eprintln!(
                "catalog_helper must be invoked through a packaged catalog-helper alias; got {invoked_name:?}"
            );
            ExitCode::FAILURE
        }
    }
}
