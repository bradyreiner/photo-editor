use std::fs::{self, OpenOptions};
use std::io::Write;
use std::path::Path;
use std::time::{SystemTime, UNIX_EPOCH};

use photo_catalog::{run_catalog_helper, AppRuntimePaths, CatalogRuntime, RecipeMigrationRecord};
use serde_json::{json, Value};
use sha2::{Digest, Sha256};

pub(crate) fn main() -> std::process::ExitCode {
    run_catalog_helper(run)
}

async fn run() -> Result<(), Box<dyn std::error::Error + Send + Sync>> {
    let arguments = photo_catalog::helper_arguments::read_helper_arguments()?;
    if arguments.len() != 14 {
        return Err(
            "usage: accept_recipe_migration <asset_id> <legacy_recipe_json> \
             <canonical_recipe_payload> <canonical_cache_key> \
             <canonical_provenance_payload> \
             <source_recipe_version> <source_renderer> <target_renderer> \
             <profile_identities_json> <plan_hash> <evidence_hash> \
             <approval_time> <evidence_json>"
                .into(),
        );
    }
    let paths = AppRuntimePaths::from_env();
    paths.ensure_directories()?;
    let catalog = CatalogRuntime::open(&paths).await?;
    let timestamp = unix_epoch_millis()?;
    let backup_path = paths
        .backup_dir
        .join(format!("pre-recipe-migration-{timestamp}.dump"));
    catalog.backup(&backup_path).await?;
    if std::fs::metadata(&backup_path)?.len() == 0 {
        return Err("pre-migration PostgreSQL backup is empty".into());
    }

    let evidence_hash = required(&arguments, 11, "evidence hash")?;
    let evidence_path = paths
        .backup_dir
        .join("migration-evidence")
        .join(format!("{evidence_hash}.json"));
    let evidence_bytes = required(&arguments, 13, "evidence JSON")?.as_bytes();
    verify_evidence_identity(evidence_bytes, evidence_hash)?;
    write_immutable_evidence(&evidence_path, evidence_bytes, timestamp)?;

    let record = RecipeMigrationRecord {
        asset_id: required(&arguments, 1, "asset id")?.to_string(),
        source_recipe_version: required(&arguments, 6, "source recipe version")?.parse::<i32>()?,
        source_renderer: required(&arguments, 7, "source renderer")?.to_string(),
        target_renderer: required(&arguments, 8, "target renderer")?.to_string(),
        profile_identities: serde_json::from_str(required(&arguments, 9, "profile identities")?)?,
        plan_hash: required(&arguments, 10, "plan hash")?.to_string(),
        evidence_hash: evidence_hash.to_string(),
        approval_time: Some(required(&arguments, 12, "approval time")?.to_string()),
        rollback_pointer: backup_path.display().to_string(),
        status: "accepted".to_string(),
        created_at: String::new(),
        updated_at: String::new(),
    };
    let legacy_recipe: Value = serde_json::from_str(required(&arguments, 2, "legacy recipe")?)?;
    catalog
        .accept_recipe_migration(
            &legacy_recipe,
            required(&arguments, 3, "canonical recipe")?,
            required(&arguments, 4, "canonical cache key")?,
            required(&arguments, 5, "canonical provenance payload")?,
            &record,
        )
        .await?;
    println!(
        "{}",
        serde_json::to_string(&json!({
            "asset_id": record.asset_id,
            "backup_path": backup_path,
            "evidence_path": evidence_path,
            "status": "accepted",
        }))?
    );
    Ok(())
}

fn required<'a>(
    arguments: &'a [String],
    index: usize,
    label: &str,
) -> Result<&'a str, Box<dyn std::error::Error + Send + Sync>> {
    arguments
        .get(index)
        .map(String::as_str)
        .filter(|value| !value.trim().is_empty())
        .ok_or_else(|| format!("missing {label}").into())
}

fn unix_epoch_millis() -> Result<u128, std::time::SystemTimeError> {
    Ok(SystemTime::now().duration_since(UNIX_EPOCH)?.as_millis())
}

fn verify_evidence_identity(
    bytes: &[u8],
    expected_hash: &str,
) -> Result<(), Box<dyn std::error::Error + Send + Sync>> {
    if format!("{:x}", Sha256::digest(bytes)) != expected_hash {
        return Err("migration evidence bytes do not match the supplied SHA-256".into());
    }
    Ok(())
}

fn write_immutable_evidence(
    path: &Path,
    bytes: &[u8],
    timestamp: u128,
) -> Result<(), Box<dyn std::error::Error + Send + Sync>> {
    if path.exists() {
        if fs::read(path)? == bytes {
            return Ok(());
        }
        return Err("existing migration evidence does not match its identity".into());
    }
    let parent = path
        .parent()
        .ok_or("migration evidence path has no parent")?;
    fs::create_dir_all(parent)?;
    let temporary = parent.join(format!(".migration-evidence-{timestamp}.tmp"));
    let mut file = OpenOptions::new()
        .create_new(true)
        .write(true)
        .open(&temporary)?;
    file.write_all(bytes)?;
    file.sync_all()?;
    fs::rename(&temporary, path)?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn migration_evidence_is_immutable_and_idempotent() {
        let temporary = tempfile::tempdir().expect("temporary evidence root");
        let path = temporary.path().join("evidence/hash.json");
        write_immutable_evidence(&path, br#"{"evidence":"first"}"#, 1)
            .expect("write immutable evidence");
        write_immutable_evidence(&path, br#"{"evidence":"first"}"#, 2)
            .expect("repeat identical evidence");
        assert!(
            write_immutable_evidence(&path, br#"{"evidence":"different"}"#, 3).is_err(),
            "an existing evidence identity must reject different bytes"
        );
        assert_eq!(
            fs::read(&path).expect("read immutable evidence"),
            br#"{"evidence":"first"}"#
        );
        let bytes = br#"{"evidence":"first"}"#;
        let identity = format!("{:x}", Sha256::digest(bytes));
        verify_evidence_identity(bytes, &identity).expect("matching SHA-256 evidence");
        assert!(verify_evidence_identity(bytes, &"0".repeat(64)).is_err());
    }
}
