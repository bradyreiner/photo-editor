use std::process::Command;

#[test]
fn health_does_not_report_ready_or_create_a_catalog_for_invalid_authority() {
    let runtime = tempfile::tempdir().unwrap();
    let output = Command::new(env!("CARGO_BIN_EXE_catalog_export_jobs"))
        .arg("health")
        .env("PHOTO_EDITOR_APP_DATA_DIR", runtime.path())
        .env("PHOTO_EDITOR_LOG_DIR", runtime.path().join("logs"))
        .env("PHOTO_EDITOR_BACKUP_DIR", runtime.path().join("backups"))
        .env("PHOTO_EDITOR_DATABASE_URL", "invalid-database-authority")
        .output()
        .unwrap();

    assert!(!output.status.success());
    assert!(!String::from_utf8_lossy(&output.stdout).contains("\"ready\":true"));
    assert!(!runtime.path().join("postgres").exists());
}
