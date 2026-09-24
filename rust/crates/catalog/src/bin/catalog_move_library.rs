use std::collections::HashSet;
use std::env;
use std::fs::{self, File};
use std::io::{BufReader, Read};
use std::path::{Path, PathBuf};
use std::time::{SystemTime, UNIX_EPOCH};

use photo_catalog::{
    run_catalog_helper, AppRuntimePaths, CatalogRepository, CatalogRuntime, ImportMode,
};
use sha2::{Digest, Sha256};

pub(crate) fn main() -> std::process::ExitCode {
    run_catalog_helper(run)
}

async fn run() -> Result<(), Box<dyn std::error::Error + Send + Sync>> {
    let arguments: Vec<String> = env::args().collect();
    let old_root = required_path(&arguments, 1, "missing current library root")?;
    let new_root = required_path(&arguments, 2, "missing destination library root")?;
    validate_roots(&old_root, &new_root)?;

    fs::create_dir_all(&new_root)?;
    if fs::read_dir(&new_root)?.next().is_some() {
        return Err("the destination Photo Library folder must be empty".into());
    }

    let runtime_paths = AppRuntimePaths::from_env();
    runtime_paths.ensure_directories()?;
    let catalog = CatalogRuntime::open(&runtime_paths).await?;
    let backup_path = runtime_paths.backup_dir.join(format!(
        "pre-library-move-{}.dump",
        SystemTime::now().duration_since(UNIX_EPOCH)?.as_millis()
    ));
    catalog.backup(&backup_path).await?;

    let snapshots = catalog.list_asset_snapshots(1_000_000).await?;
    let managed_assets = snapshots
        .into_iter()
        .filter(|snapshot| snapshot.asset.import_mode == ImportMode::Managed)
        .collect::<Vec<_>>();
    let destination_parent = new_root
        .parent()
        .ok_or("the destination Photo Library requires a parent folder")?;
    let destination_name = new_root
        .file_name()
        .and_then(|name| name.to_str())
        .ok_or("the destination Photo Library requires a valid folder name")?;
    let staging_identity = SystemTime::now().duration_since(UNIX_EPOCH)?.as_nanos();
    let staging_root = destination_parent.join(format!(
        ".{destination_name}.framehouse-library-migration-{}-{staging_identity}",
        std::process::id()
    ));
    fs::create_dir(&staging_root)?;
    let _staging_cleanup = TemporaryDirectoryCleanup::new(staging_root.clone());

    let mut copied_files = 0_usize;
    let mut copied_bytes = 0_u64;
    let mut seen_sources = HashSet::new();
    for snapshot in &managed_assets {
        let asset = &snapshot.asset;
        let source_root = PathBuf::from(&asset.location.root_path);
        let relative_root = source_root.strip_prefix(&old_root).map_err(|_| {
            format!(
                "managed asset {} is outside the current Photo Library",
                asset.asset_id
            )
        })?;
        let relative_file = Path::new(&asset.location.relative_path);
        if relative_file.is_absolute()
            || relative_file
                .components()
                .any(|component| matches!(component, std::path::Component::ParentDir))
        {
            return Err(format!("asset {} has an unsafe relative path", asset.asset_id).into());
        }
        let source = source_root.join(relative_file);
        if !seen_sources.insert(source.clone()) {
            continue;
        }
        if !source.is_file() {
            return Err(format!("managed original is unavailable: {}", source.display()).into());
        }
        let destination = staging_root.join(relative_root).join(relative_file);
        if let Some(parent) = destination.parent() {
            fs::create_dir_all(parent)?;
        }
        fs::copy(&source, &destination)?;
        if sha256_file(&source)? != sha256_file(&destination)? {
            return Err(format!("SHA-256 verification failed for {}", source.display()).into());
        }
        copied_bytes += fs::metadata(&source)?.len();
        copied_files += 1;
    }

    if fs::read_dir(&new_root)?.next().is_some() {
        return Err("the destination Photo Library folder changed during migration".into());
    }
    fs::remove_dir(&new_root)?;
    if let Err(error) = fs::rename(&staging_root, &new_root) {
        let _ = fs::create_dir_all(&new_root);
        return Err(error.into());
    }

    let updated_assets = match catalog.relocate_managed_library(&old_root, &new_root).await {
        Ok(updated_assets) => updated_assets,
        Err(error) => {
            let cleanup_result = fs::remove_dir_all(&new_root);
            let recreate_result = fs::create_dir_all(&new_root);
            if let Err(cleanup_error) = cleanup_result {
                return Err(format!(
                    "catalog rejected the library move ({error}); copied destination cleanup failed: {cleanup_error}"
                )
                .into());
            }
            if let Err(recreate_error) = recreate_result {
                return Err(format!(
                    "catalog rejected the library move ({error}); empty destination recreation failed: {recreate_error}"
                )
                .into());
            }
            return Err(error.into());
        }
    };
    println!(
        "FRAMEHOUSE_LIBRARY_MOVE_RESULT={}",
        serde_json::to_string(&serde_json::json!({
            "schema": "photo-library-move:v1",
            "oldRoot": old_root,
            "newRoot": new_root,
            "copiedFiles": copied_files,
            "copiedBytes": copied_bytes,
            "updatedAssets": updated_assets,
            "verificationAlgorithm": "SHA-256",
            "catalogBackup": backup_path,
            "oldLibraryRetained": true
        }))?
    );
    Ok(())
}

struct TemporaryDirectoryCleanup {
    path: PathBuf,
}

impl TemporaryDirectoryCleanup {
    fn new(path: PathBuf) -> Self {
        Self { path }
    }
}

impl Drop for TemporaryDirectoryCleanup {
    fn drop(&mut self) {
        if self.path.is_dir() {
            let _ = fs::remove_dir_all(&self.path);
        }
    }
}

fn required_path(
    arguments: &[String],
    index: usize,
    message: &str,
) -> Result<PathBuf, Box<dyn std::error::Error + Send + Sync>> {
    arguments
        .get(index)
        .map(PathBuf::from)
        .filter(|path| path.is_absolute())
        .ok_or_else(|| message.into())
}

fn validate_roots(
    old_root: &Path,
    new_root: &Path,
) -> Result<(), Box<dyn std::error::Error + Send + Sync>> {
    if old_root == new_root || old_root.starts_with(new_root) || new_root.starts_with(old_root) {
        return Err("the current and destination Photo Libraries must be separate".into());
    }
    if !old_root.is_dir() {
        return Err("the current Photo Library is unavailable".into());
    }
    Ok(())
}

fn sha256_file(path: &Path) -> std::io::Result<[u8; 32]> {
    let mut reader = BufReader::new(File::open(path)?);
    let mut digest = Sha256::new();
    let mut buffer = [0_u8; 64 * 1024];
    loop {
        let read = reader.read(&mut buffer)?;
        if read == 0 {
            break;
        }
        digest.update(&buffer[..read]);
    }
    Ok(digest.finalize().into())
}
