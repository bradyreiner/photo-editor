pub mod helper_arguments;
mod importer;
mod local_database;
pub mod observability;

use std::fs;
use std::future::Future;
use std::panic::{self, AssertUnwindSafe};
use std::path::{Path, PathBuf};
use std::process::ExitCode;

use serde::{Deserialize, Serialize};
use serde_json::Value;
use sqlx::migrate::Migrator;
use sqlx::postgres::{PgPoolOptions, PgRow};
use sqlx::{PgPool, Postgres, QueryBuilder, Row};

pub use importer::{
    discover_assets, extract_metadata, fallback_metadata, is_supported_photo_path,
    plan_import_destinations, DuplicatePolicy, ExtractedMetadata, ImportDestinationPlan,
    ImportPlanningSummary, ImportRequest, ImportSummary,
};
pub use local_database::{
    ensure_external_database_ready, CatalogDatabaseStatus, LocalCatalogDatabaseManager,
    LocalPostgresState, POSTGRES_BIN_DIR_ENV,
};

const DEFAULT_DATABASE_URL: &str =
    "postgresql://photo_editor:photo_editor@localhost:5433/photo_editor";
const DEFAULT_MAX_CONNECTIONS: u32 = 10;
const ASSET_IMPORT_BATCH_SIZE: usize = 1000;
pub const EMPTY_CANONICAL_RECIPE_PROVENANCE_V1: &str =
    r#"{"schema":"photo-editor-edit-recipe-provenance:v1","presetApplications":[]}"#;
pub const APP_DATA_DIR_ENV: &str = "PHOTO_EDITOR_APP_DATA_DIR";
pub const LOG_DIR_ENV: &str = "PHOTO_EDITOR_LOG_DIR";
pub const BACKUP_DIR_ENV: &str = "PHOTO_EDITOR_BACKUP_DIR";
pub const MANAGED_MEDIA_DIR_ENV: &str = "PHOTO_EDITOR_MANAGED_MEDIA_DIR";
pub const ORIGINALS_BACKUP_DIR_ENV: &str = "PHOTO_EDITOR_ORIGINALS_BACKUP_DIR";

pub fn run_catalog_helper<F, Fut, Error>(run: F) -> ExitCode
where
    F: FnOnce() -> Fut,
    Fut: Future<Output = Result<(), Error>>,
    Error: std::fmt::Display,
{
    let previous_hook = panic::take_hook();
    panic::set_hook(Box::new(|_| {}));
    let outcome = panic::catch_unwind(AssertUnwindSafe(|| {
        let runtime = tokio::runtime::Builder::new_multi_thread()
            .enable_all()
            .build()
            .map_err(|error| error.to_string())?;
        runtime
            .block_on(run())
            .map_err(|error| sanitize_catalog_diagnostic(&error.to_string()))
    }));
    panic::set_hook(previous_hook);

    match outcome {
        Ok(Ok(())) => ExitCode::SUCCESS,
        Ok(Err(error)) => {
            eprintln!("Catalog helper failed: {error}");
            ExitCode::FAILURE
        }
        Err(_) => {
            eprintln!(
                "Catalog helper failed unexpectedly. Retry once, then inspect owner-only logs or restore a verified backup."
            );
            ExitCode::FAILURE
        }
    }
}

fn sanitize_catalog_diagnostic(message: &str) -> String {
    let mut sanitized = message.to_string();
    for scheme in ["postgresql://", "postgres://"] {
        let mut search_from = 0;
        while let Some(relative_start) = sanitized[search_from..].find(scheme) {
            let start = search_from + relative_start + scheme.len();
            let Some(relative_at) = sanitized[start..].find('@') else {
                break;
            };
            let at = start + relative_at;
            let credentials = &sanitized[start..at];
            let Some(colon) = credentials.find(':') else {
                search_from = at + 1;
                continue;
            };
            let password_start = start + colon + 1;
            sanitized.replace_range(password_start..at, "<redacted>");
            search_from = password_start + "<redacted>@".len();
        }
    }
    sanitized
}

fn validate_canonical_edit_recipe_v13_input(
    asset_id: &str,
    recipe_payload: &str,
    cache_key: &str,
    provenance_payload: &str,
) -> Result<(), sqlx::Error> {
    if asset_id.trim().is_empty() {
        return Err(sqlx::Error::Protocol(
            "canonical v13 recipe requires a non-empty asset id".to_string(),
        ));
    }
    if recipe_payload.trim().is_empty() {
        return Err(sqlx::Error::Protocol(
            "canonical v13 recipe requires a non-empty payload".to_string(),
        ));
    }
    if cache_key.trim().is_empty() {
        return Err(sqlx::Error::Protocol(
            "canonical v13 recipe requires a non-empty cache key".to_string(),
        ));
    }
    let provenance: Value = serde_json::from_str(provenance_payload)
        .map_err(|error| sqlx::Error::Decode(Box::new(error)))?;
    let object = provenance.as_object().ok_or_else(|| {
        sqlx::Error::Protocol("canonical v13 provenance must be a JSON object".to_string())
    })?;
    let valid_version = match object.get("schema").and_then(Value::as_str) {
        Some("photo-editor-edit-recipe-provenance:v1") => object.len() == 2,
        Some("photo-editor-edit-recipe-provenance:v2") => {
            object.len() == 3
                && object
                    .get("history")
                    .and_then(Value::as_object)
                    .is_some_and(|history| {
                        history.len() == 2
                            && history
                                .get("activeBranchId")
                                .and_then(Value::as_str)
                                .is_some_and(|branch| !branch.trim().is_empty())
                            && history.get("snapshots").is_some_and(Value::is_array)
                    })
        }
        _ => false,
    };
    if !valid_version
        || !object
            .get("presetApplications")
            .is_some_and(Value::is_array)
    {
        return Err(sqlx::Error::Protocol(
            "canonical v13 provenance does not match the v1 or v2 schema".to_string(),
        ));
    }
    Ok(())
}

fn validate_recipe_migration_record(record: &RecipeMigrationRecord) -> Result<(), sqlx::Error> {
    let required = [
        ("asset id", record.asset_id.trim()),
        ("source renderer", record.source_renderer.trim()),
        ("target renderer", record.target_renderer.trim()),
        ("plan hash", record.plan_hash.trim()),
        ("evidence hash", record.evidence_hash.trim()),
        ("rollback pointer", record.rollback_pointer.trim()),
    ];
    if let Some((field, _)) = required.iter().find(|(_, value)| value.is_empty()) {
        return Err(sqlx::Error::Protocol(format!(
            "recipe migration record requires {field}"
        )));
    }
    if record.source_recipe_version <= 0 {
        return Err(sqlx::Error::Protocol(
            "recipe migration source version must be positive".to_string(),
        ));
    }
    if !matches!(
        record.status.as_str(),
        "candidate" | "accepted" | "rejected" | "rolled_back"
    ) {
        return Err(sqlx::Error::Protocol(
            "recipe migration status is invalid".to_string(),
        ));
    }
    if record.status == "accepted" && record.approval_time.as_deref().is_none_or(str::is_empty) {
        return Err(sqlx::Error::Protocol(
            "accepted recipe migration requires approval time".to_string(),
        ));
    }
    Ok(())
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub enum ImportMode {
    Referenced,
    Managed,
}

impl ImportMode {
    fn as_database_value(&self) -> &'static str {
        match self {
            Self::Referenced => "referenced",
            Self::Managed => "managed",
        }
    }

    fn from_database_value(value: &str) -> Result<Self, sqlx::Error> {
        match value {
            "referenced" => Ok(Self::Referenced),
            "managed" => Ok(Self::Managed),
            value => Err(sqlx::Error::Protocol(format!(
                "catalog data integrity error: unknown import_mode '{value}'"
            ))),
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub struct AssetLocation {
    pub drive_id: String,
    pub root_path: String,
    pub relative_path: String,
    pub online: bool,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct CatalogAssetRecord {
    pub asset_id: String,
    pub capture_year: u16,
    pub capture_month: u8,
    pub capture_day: u8,
    pub camera_make: String,
    pub camera_model: String,
    pub lens_model: String,
    pub iso: Option<i32>,
    pub shutter_speed: String,
    pub aperture: Option<f64>,
    pub import_mode: ImportMode,
    pub location: AssetLocation,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub struct DriveRecord {
    pub drive_id: String,
    pub display_name: String,
    pub mount_path: String,
    pub online: bool,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct CatalogAssetSnapshot {
    #[serde(flatten)]
    pub asset: CatalogAssetRecord,
    pub edit_recipe: Option<Value>,
    pub canonical_edit_recipe_v13: Option<CanonicalEditRecipeV13>,
    pub imported_at: String,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub struct CanonicalEditRecipeV13 {
    pub asset_id: String,
    pub recipe_payload: String,
    pub cache_key: String,
    pub provenance_payload: String,
    pub updated_at: String,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub struct RecipeMigrationRecord {
    pub asset_id: String,
    pub source_recipe_version: i32,
    pub source_renderer: String,
    pub target_renderer: String,
    pub profile_identities: Vec<String>,
    pub plan_hash: String,
    pub evidence_hash: String,
    pub approval_time: Option<String>,
    pub rollback_pointer: String,
    pub status: String,
    pub created_at: String,
    pub updated_at: String,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub struct PersistedProject {
    pub name: String,
    pub created_at: String,
    pub asset_ids: Vec<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub struct AssetReviewStateRecord {
    pub asset_id: String,
    pub rating: i32,
    pub picked: bool,
    pub rejected: bool,
    pub color_label: String,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub struct AssetRelinkRecord {
    pub asset_id: String,
    pub relinked_file_path: String,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct PersistedExportJob {
    pub job_id: String,
    pub job_kind: String,
    pub asset_id: String,
    pub media_type: String,
    pub display_label: String,
    pub source_path: String,
    pub edited_preview_source: String,
    #[serde(rename = "adjustment_payload", alias = "recipe_json")]
    pub recipe_json: Option<Value>,
    pub metadata_json: Option<Value>,
    pub preset_name: String,
    pub destination_path: String,
    pub output_path: String,
    pub status: String,
    pub error_message: String,
    pub progress: i32,
    pub created_at: String,
    pub updated_at: String,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct PersistedTimelineDocument {
    pub timeline_id: String,
    pub asset_id: String,
    pub name: String,
    pub document_path: String,
    pub render_path: String,
    pub poster_path: String,
    pub duration_seconds: f64,
    pub document_json: Option<Value>,
    pub created_at: String,
    pub updated_at: String,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct PersistedCarouselDocument {
    pub document_id: String,
    pub name: String,
    pub document_format: String,
    pub slide_count: i32,
    pub document_json: Value,
    pub created_at: String,
    pub updated_at: String,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub struct CatalogIntegrityFinding {
    pub record_type: String,
    pub record_id: String,
    pub field: String,
    pub detail: String,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub struct CatalogIntegrityReport {
    pub schema: String,
    pub scanned_record_count: i64,
    pub findings: Vec<CatalogIntegrityFinding>,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub struct PostgresCatalogConfig {
    pub database_url: String,
    pub max_connections: u32,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub struct AppRuntimePaths {
    pub app_data_dir: PathBuf,
    pub log_dir: PathBuf,
    pub backup_dir: PathBuf,
}

impl Default for PostgresCatalogConfig {
    fn default() -> Self {
        Self {
            database_url: DEFAULT_DATABASE_URL.to_string(),
            max_connections: DEFAULT_MAX_CONNECTIONS,
        }
    }
}

impl PostgresCatalogConfig {
    pub fn from_env() -> Self {
        let mut config = Self::default();

        if let Ok(database_url) = std::env::var("PHOTO_EDITOR_DATABASE_URL") {
            config.database_url = database_url;
        }

        if let Ok(max_connections) = std::env::var("PHOTO_EDITOR_DATABASE_MAX_CONNECTIONS") {
            if let Ok(parsed) = max_connections.parse::<u32>() {
                config.max_connections = parsed;
            }
        }

        config
    }
}

impl AppRuntimePaths {
    pub fn from_env() -> Self {
        let app_data_dir = std::env::var_os(APP_DATA_DIR_ENV)
            .map(PathBuf::from)
            .unwrap_or_else(default_app_data_dir);
        let log_dir = std::env::var_os(LOG_DIR_ENV)
            .map(PathBuf::from)
            .unwrap_or_else(|| app_data_dir.join("logs"));
        let backup_dir = std::env::var_os(BACKUP_DIR_ENV)
            .map(PathBuf::from)
            .unwrap_or_else(|| app_data_dir.join("backups"));

        Self {
            app_data_dir,
            log_dir,
            backup_dir,
        }
    }

    pub fn ensure_directories(&self) -> std::io::Result<()> {
        fs::create_dir_all(&self.app_data_dir)?;
        fs::create_dir_all(&self.log_dir)?;
        fs::create_dir_all(&self.backup_dir)?;
        Ok(())
    }
}

#[allow(async_fn_in_trait)]
pub trait CatalogRepository {
    async fn healthcheck(&self) -> Result<(), sqlx::Error>;
    async fn catalog_integrity_report(&self) -> Result<CatalogIntegrityReport, sqlx::Error>;
    async fn upsert_drive(&self, drive: &DriveRecord) -> Result<(), sqlx::Error>;
    async fn upsert_asset(&self, asset: &CatalogAssetRecord) -> Result<(), sqlx::Error>;
    async fn import_summary(&self, summary: &ImportSummary) -> Result<(), sqlx::Error>;
    async fn list_assets(&self, limit: i64) -> Result<Vec<CatalogAssetRecord>, sqlx::Error>;
    async fn list_asset_snapshots(
        &self,
        limit: i64,
    ) -> Result<Vec<CatalogAssetSnapshot>, sqlx::Error>;
    async fn upsert_edit_recipe(&self, asset_id: &str, recipe: &Value) -> Result<(), sqlx::Error>;
    async fn upsert_canonical_edit_recipe_v13(
        &self,
        asset_id: &str,
        recipe_payload: &str,
        cache_key: &str,
        provenance_payload: &str,
    ) -> Result<(), sqlx::Error>;
    async fn canonical_edit_recipe_v13(
        &self,
        asset_id: &str,
    ) -> Result<Option<CanonicalEditRecipeV13>, sqlx::Error>;
    async fn upsert_edit_recipe_compatibility_pair(
        &self,
        asset_id: &str,
        legacy_recipe: &Value,
        canonical_recipe_payload: &str,
        canonical_cache_key: &str,
        canonical_provenance_payload: &str,
    ) -> Result<(), sqlx::Error>;
    async fn list_recipe_migration_records(
        &self,
    ) -> Result<Vec<RecipeMigrationRecord>, sqlx::Error>;
    async fn upsert_recipe_migration_record(
        &self,
        record: &RecipeMigrationRecord,
    ) -> Result<(), sqlx::Error>;
    async fn list_drives(&self) -> Result<Vec<DriveRecord>, sqlx::Error>;
    async fn list_projects(&self) -> Result<Vec<PersistedProject>, sqlx::Error>;
    async fn upsert_project(&self, name: &str) -> Result<(), sqlx::Error>;
    async fn add_asset_to_project(
        &self,
        asset_id: &str,
        project_name: &str,
    ) -> Result<(), sqlx::Error>;
    async fn remove_asset_from_project(
        &self,
        asset_id: &str,
        project_name: &str,
    ) -> Result<(), sqlx::Error>;
    async fn delete_asset(&self, asset_id: &str) -> Result<(), sqlx::Error>;
    async fn list_asset_review_states(&self) -> Result<Vec<AssetReviewStateRecord>, sqlx::Error>;
    async fn upsert_asset_review_state(
        &self,
        state: &AssetReviewStateRecord,
    ) -> Result<(), sqlx::Error>;
    async fn list_asset_relinks(&self) -> Result<Vec<AssetRelinkRecord>, sqlx::Error>;
    async fn set_asset_relink(
        &self,
        asset_id: &str,
        relinked_file_path: &str,
    ) -> Result<(), sqlx::Error>;
    async fn remove_asset_relink(&self, asset_id: &str) -> Result<(), sqlx::Error>;
    async fn list_export_jobs(&self) -> Result<Vec<PersistedExportJob>, sqlx::Error>;
    async fn upsert_export_job(&self, job: &PersistedExportJob) -> Result<(), sqlx::Error>;
    async fn import_legacy_export_job(&self, job: &PersistedExportJob)
        -> Result<bool, sqlx::Error>;
    async fn delete_export_job(&self, job_id: &str) -> Result<(), sqlx::Error>;
    async fn list_timeline_documents(&self) -> Result<Vec<PersistedTimelineDocument>, sqlx::Error>;
    async fn get_timeline_document(
        &self,
        timeline_id: &str,
    ) -> Result<Option<PersistedTimelineDocument>, sqlx::Error>;
    async fn upsert_timeline_document(
        &self,
        document: &PersistedTimelineDocument,
    ) -> Result<(), sqlx::Error>;
    async fn replace_timeline_documents(
        &self,
        documents: &[PersistedTimelineDocument],
    ) -> Result<(), sqlx::Error>;
    async fn list_carousel_documents(&self) -> Result<Vec<PersistedCarouselDocument>, sqlx::Error>;
    async fn get_carousel_document(
        &self,
        document_id: &str,
    ) -> Result<Option<PersistedCarouselDocument>, sqlx::Error>;
    async fn upsert_carousel_document(
        &self,
        document: &PersistedCarouselDocument,
    ) -> Result<(), sqlx::Error>;
}

pub struct CatalogService {
    pool: PgPool,
}

pub struct CatalogRuntime {
    repository: CatalogService,
}

impl CatalogRuntime {
    pub async fn open(paths: &AppRuntimePaths) -> Result<Self, sqlx::Error> {
        paths.ensure_directories().map_err(sqlx::Error::Io)?;
        let database_url = LocalCatalogDatabaseManager::from_env()
            .ensure_ready_database_url()
            .await
            .map_err(|error| sqlx::Error::Protocol(error.to_string()))?;
        let repository = CatalogService::connect(&PostgresCatalogConfig {
            database_url,
            max_connections: DEFAULT_MAX_CONNECTIONS,
        })
        .await?;
        repository.initialize_schema().await?;
        repository.healthcheck().await?;
        Ok(Self { repository })
    }

    pub fn backend_name(&self) -> &'static str {
        "postgresql"
    }

    pub async fn backup(&self, destination: impl AsRef<Path>) -> Result<(), sqlx::Error> {
        LocalCatalogDatabaseManager::from_env()
            .backup_to(destination.as_ref())
            .await
            .map_err(|error| sqlx::Error::Protocol(error.to_string()))
    }

    pub async fn relocate_managed_library(
        &self,
        old_root: impl AsRef<Path>,
        new_root: impl AsRef<Path>,
    ) -> Result<usize, sqlx::Error> {
        self.repository
            .relocate_managed_library(old_root, new_root)
            .await
    }

    pub async fn accept_recipe_migration(
        &self,
        legacy_recipe: &Value,
        canonical_recipe_payload: &str,
        canonical_cache_key: &str,
        canonical_provenance_payload: &str,
        record: &RecipeMigrationRecord,
    ) -> Result<(), sqlx::Error> {
        self.repository
            .accept_recipe_migration(
                legacy_recipe,
                canonical_recipe_payload,
                canonical_cache_key,
                canonical_provenance_payload,
                record,
            )
            .await
    }
}

impl CatalogService {
    pub async fn connect(config: &PostgresCatalogConfig) -> Result<Self, sqlx::Error> {
        let pool = PgPoolOptions::new()
            .max_connections(config.max_connections)
            .connect(&config.database_url)
            .await?;

        Ok(Self { pool })
    }

    pub fn pool(&self) -> &PgPool {
        &self.pool
    }

    pub async fn initialize_schema(&self) -> Result<(), sqlx::migrate::MigrateError> {
        let migrator =
            Migrator::new(Path::new(env!("CARGO_MANIFEST_DIR")).join("migrations")).await?;
        migrator.run(&self.pool).await?;
        Ok(())
    }

    pub async fn healthcheck(&self) -> Result<(), sqlx::Error> {
        sqlx::query("SELECT 1").execute(&self.pool).await?;
        Ok(())
    }

    pub async fn catalog_integrity_report(&self) -> Result<CatalogIntegrityReport, sqlx::Error> {
        let count_row = sqlx::query(
            r#"
            SELECT
                (SELECT COUNT(*) FROM assets)
              + (SELECT COUNT(*) FROM drives)
              + (SELECT COUNT(*) FROM projects)
              + (SELECT COUNT(*) FROM asset_review_states)
              + (SELECT COUNT(*) FROM asset_relinks)
              + (SELECT COUNT(*) FROM export_jobs)
              + (SELECT COUNT(*) FROM timeline_documents)
              + (SELECT COUNT(*) FROM carousel_documents)
              + (SELECT COUNT(*) FROM recipe_migration_records)
                AS scanned_record_count
            "#,
        )
        .fetch_one(&self.pool)
        .await?;
        let scanned_record_count: i64 = count_row.try_get("scanned_record_count")?;
        let rows = sqlx::query(
            r#"
            SELECT 'asset' AS record_type, asset_id AS record_id,
                   'capture_date' AS field, 'missing or invalid Gregorian capture date' AS detail
            FROM assets
            WHERE capture_year IS NULL OR capture_month IS NULL OR capture_day IS NULL
               OR capture_year NOT BETWEEN 1 AND 9999
               OR capture_month NOT BETWEEN 1 AND 12
               OR capture_day < 1
               OR capture_day > CASE capture_month
                    WHEN 1 THEN 31 WHEN 3 THEN 31 WHEN 5 THEN 31 WHEN 7 THEN 31
                    WHEN 8 THEN 31 WHEN 10 THEN 31 WHEN 12 THEN 31
                    WHEN 4 THEN 30 WHEN 6 THEN 30 WHEN 9 THEN 30 WHEN 11 THEN 30
                    WHEN 2 THEN CASE
                        WHEN capture_year % 4 = 0
                         AND (capture_year % 100 <> 0 OR capture_year % 400 = 0)
                        THEN 29 ELSE 28 END
                    ELSE 0 END
            UNION ALL
            SELECT 'asset', asset_id, 'import_mode', 'unknown import mode'
            FROM assets WHERE import_mode NOT IN ('referenced', 'managed')
            UNION ALL
            SELECT 'asset', asset_id, 'iso', 'ISO must be positive or null'
            FROM assets WHERE iso IS NOT NULL AND iso <= 0
            UNION ALL
            SELECT 'asset', asset_id, 'aperture', 'aperture must be finite and positive or null'
            FROM assets
            WHERE aperture IS NOT NULL
              AND NOT (aperture > 0 AND aperture < 'Infinity'::DOUBLE PRECISION)
            UNION ALL
            SELECT 'asset', asset_id, 'rating', 'rating must be between 0 and 5'
            FROM assets WHERE rating NOT BETWEEN 0 AND 5
            UNION ALL
            SELECT 'asset_review_state', asset_id, 'rating', 'rating must be between 0 and 5'
            FROM asset_review_states WHERE rating NOT BETWEEN 0 AND 5
            UNION ALL
            SELECT 'asset_review_state', asset_id, 'picked/rejected', 'flags are mutually exclusive'
            FROM asset_review_states WHERE picked AND rejected
            UNION ALL
            SELECT 'asset_relink', asset_id, 'relinked_file_path', 'path is null'
            FROM asset_relinks WHERE relinked_file_path IS NULL
            UNION ALL
            SELECT 'export_job', job_id, 'progress', 'progress must be between 0 and 100'
            FROM export_jobs WHERE progress NOT BETWEEN 0 AND 100
            UNION ALL
            SELECT 'timeline_document', timeline_id, 'duration_seconds',
                   'duration must be finite and non-negative'
            FROM timeline_documents
            WHERE NOT (duration_seconds >= 0
                   AND duration_seconds < 'Infinity'::DOUBLE PRECISION)
            UNION ALL
            SELECT 'carousel_document', document_id, 'slide_count',
                   'slide count must be non-negative'
            FROM carousel_documents WHERE slide_count < 0
            UNION ALL
            SELECT 'recipe_migration', asset_id, 'profile_identities',
                   'profile identities must be a JSON array of strings'
            FROM recipe_migration_records
            WHERE jsonb_typeof(profile_identities) <> 'array'
               OR CASE WHEN jsonb_typeof(profile_identities) = 'array'
                    THEN jsonb_path_exists(
                        profile_identities,
                        '$[*] ? (@.type() != "string")'
                    )
                    ELSE FALSE END
            ORDER BY record_type, record_id, field
            "#,
        )
        .fetch_all(&self.pool)
        .await?;
        let findings = rows
            .into_iter()
            .map(|row| {
                Ok(CatalogIntegrityFinding {
                    record_type: row.try_get("record_type")?,
                    record_id: row.try_get("record_id")?,
                    field: row.try_get("field")?,
                    detail: row.try_get("detail")?,
                })
            })
            .collect::<Result<Vec<_>, sqlx::Error>>()?;

        Ok(CatalogIntegrityReport {
            schema: "photo-editor-catalog-integrity-report:v1".to_string(),
            scanned_record_count,
            findings,
        })
    }

    pub async fn relocate_managed_library(
        &self,
        old_root: impl AsRef<Path>,
        new_root: impl AsRef<Path>,
    ) -> Result<usize, sqlx::Error> {
        let old_root = old_root.as_ref();
        let new_root = new_root.as_ref();
        let rows = sqlx::query(
            "SELECT DISTINCT drive_id, root_path
             FROM assets
             WHERE import_mode = 'managed'",
        )
        .fetch_all(&self.pool)
        .await?;

        let mut relocations = Vec::new();
        for row in rows {
            let drive_id: String = row.try_get("drive_id")?;
            let root_path: String = row.try_get("root_path")?;
            let relative_root = Path::new(&root_path)
                .strip_prefix(old_root)
                .map(PathBuf::from)
                .map_err(|_| {
                    sqlx::Error::Protocol(format!(
                        "managed asset root is outside the configured library: {root_path}"
                    ))
                })?;
            if relative_root.as_os_str().is_empty() {
                return Err(sqlx::Error::Protocol(
                    "managed asset root must identify a library subfolder".to_string(),
                ));
            }
            relocations.push((
                drive_id,
                root_path,
                new_root.join(relative_root).to_string_lossy().to_string(),
            ));
        }

        let mut transaction = self.pool.begin().await?;
        let mut updated_assets = 0_usize;
        for (drive_id, old_asset_root, new_asset_root) in relocations {
            let result = sqlx::query(
                "UPDATE assets
                 SET root_path = $1,
                     updated_at = NOW()
                 WHERE import_mode = 'managed' AND drive_id = $2 AND root_path = $3",
            )
            .bind(&new_asset_root)
            .bind(&drive_id)
            .bind(&old_asset_root)
            .execute(&mut *transaction)
            .await?;
            updated_assets += result.rows_affected() as usize;
            sqlx::query(
                "UPDATE drives
                 SET mount_path = $1,
                     last_seen_at = NOW()
                 WHERE drive_id = $2",
            )
            .bind(&new_asset_root)
            .bind(&drive_id)
            .execute(&mut *transaction)
            .await?;
        }
        transaction.commit().await?;
        Ok(updated_assets)
    }

    pub async fn upsert_drive(&self, drive: &DriveRecord) -> Result<(), sqlx::Error> {
        sqlx::query(
            r#"
            INSERT INTO drives (drive_id, display_name, mount_path, is_online, last_seen_at)
            VALUES ($1, $2, $3, $4, NOW())
            ON CONFLICT (drive_id) DO UPDATE SET
                display_name = EXCLUDED.display_name,
                mount_path = EXCLUDED.mount_path,
                is_online = EXCLUDED.is_online,
                last_seen_at = NOW()
            "#,
        )
        .bind(&drive.drive_id)
        .bind(&drive.display_name)
        .bind(&drive.mount_path)
        .bind(drive.online)
        .execute(&self.pool)
        .await?;

        Ok(())
    }

    pub async fn upsert_asset(&self, asset: &CatalogAssetRecord) -> Result<(), sqlx::Error> {
        sqlx::query(
            r#"
            INSERT INTO assets (
                asset_id,
                drive_id,
                root_path,
                relative_path,
                import_mode,
                capture_year,
                capture_month,
                capture_day,
                camera_make,
                camera_model,
                lens_model,
                iso,
                shutter_speed,
                aperture,
                created_at,
                updated_at
            )
            VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14, NOW(), NOW())
            ON CONFLICT (asset_id) DO UPDATE SET
                drive_id = EXCLUDED.drive_id,
                root_path = EXCLUDED.root_path,
                relative_path = EXCLUDED.relative_path,
                import_mode = EXCLUDED.import_mode,
                capture_year = EXCLUDED.capture_year,
                capture_month = EXCLUDED.capture_month,
                capture_day = EXCLUDED.capture_day,
                camera_make = EXCLUDED.camera_make,
                camera_model = EXCLUDED.camera_model,
                lens_model = EXCLUDED.lens_model,
                iso = EXCLUDED.iso,
                shutter_speed = EXCLUDED.shutter_speed,
                aperture = EXCLUDED.aperture,
                updated_at = NOW()
            "#,
        )
        .bind(&asset.asset_id)
        .bind(&asset.location.drive_id)
        .bind(&asset.location.root_path)
        .bind(&asset.location.relative_path)
        .bind(asset.import_mode.as_database_value())
        .bind(i32::from(asset.capture_year))
        .bind(i32::from(asset.capture_month))
        .bind(i32::from(asset.capture_day))
        .bind(&asset.camera_make)
        .bind(&asset.camera_model)
        .bind(&asset.lens_model)
        .bind(asset.iso)
        .bind(&asset.shutter_speed)
        .bind(asset.aperture)
        .execute(&self.pool)
        .await?;

        Ok(())
    }

    pub async fn import_summary(&self, summary: &ImportSummary) -> Result<(), sqlx::Error> {
        let mut transaction = self.pool.begin().await?;

        sqlx::query(
            r#"
            INSERT INTO drives (drive_id, display_name, mount_path, is_online, last_seen_at)
            VALUES ($1, $2, $3, $4, NOW())
            ON CONFLICT (drive_id) DO UPDATE SET
                display_name = EXCLUDED.display_name,
                mount_path = EXCLUDED.mount_path,
                is_online = EXCLUDED.is_online,
                last_seen_at = NOW()
            "#,
        )
        .bind(&summary.drive.drive_id)
        .bind(&summary.drive.display_name)
        .bind(&summary.drive.mount_path)
        .bind(summary.drive.online)
        .execute(&mut *transaction)
        .await?;

        for asset_batch in summary.assets.chunks(ASSET_IMPORT_BATCH_SIZE) {
            let mut query_builder: QueryBuilder<'_, Postgres> = QueryBuilder::new(
                r#"
                INSERT INTO assets (
                    asset_id,
                    drive_id,
                    root_path,
                    relative_path,
                    import_mode,
                    capture_year,
                    capture_month,
                    capture_day,
                    camera_make,
                    camera_model,
                    lens_model,
                    iso,
                    shutter_speed,
                    aperture,
                    created_at,
                    updated_at
                )
                "#,
            );
            query_builder.push_values(asset_batch, |mut row, asset| {
                row.push_bind(&asset.asset_id)
                    .push_bind(&asset.location.drive_id)
                    .push_bind(&asset.location.root_path)
                    .push_bind(&asset.location.relative_path)
                    .push_bind(asset.import_mode.as_database_value())
                    .push_bind(i32::from(asset.capture_year))
                    .push_bind(i32::from(asset.capture_month))
                    .push_bind(i32::from(asset.capture_day))
                    .push_bind(&asset.camera_make)
                    .push_bind(&asset.camera_model)
                    .push_bind(&asset.lens_model)
                    .push_bind(asset.iso)
                    .push_bind(&asset.shutter_speed)
                    .push_bind(asset.aperture)
                    .push("NOW()")
                    .push("NOW()");
            });
            query_builder.push(
                r#"
                ON CONFLICT (asset_id) DO UPDATE SET
                    drive_id = EXCLUDED.drive_id,
                    root_path = EXCLUDED.root_path,
                    relative_path = EXCLUDED.relative_path,
                    import_mode = EXCLUDED.import_mode,
                    capture_year = EXCLUDED.capture_year,
                    capture_month = EXCLUDED.capture_month,
                    capture_day = EXCLUDED.capture_day,
                    camera_make = EXCLUDED.camera_make,
                    camera_model = EXCLUDED.camera_model,
                    lens_model = EXCLUDED.lens_model,
                    iso = EXCLUDED.iso,
                    shutter_speed = EXCLUDED.shutter_speed,
                    aperture = EXCLUDED.aperture,
                    updated_at = NOW()
                "#,
            );
            query_builder.build().execute(&mut *transaction).await?;
        }

        transaction.commit().await?;
        Ok(())
    }

    pub async fn list_assets(&self, limit: i64) -> Result<Vec<CatalogAssetRecord>, sqlx::Error> {
        let rows = sqlx::query(
            r#"
            SELECT
                a.asset_id,
                a.capture_year,
                a.capture_month,
                a.capture_day,
                a.camera_make,
                a.camera_model,
                a.lens_model,
                a.iso,
                a.shutter_speed,
                a.aperture,
                a.import_mode,
                d.drive_id,
                a.root_path,
                a.relative_path,
                d.is_online
            FROM assets a
            INNER JOIN drives d ON d.drive_id = a.drive_id
            ORDER BY a.capture_year DESC, a.capture_month DESC, a.capture_day DESC, a.asset_id ASC
            LIMIT $1
            "#,
        )
        .bind(limit)
        .fetch_all(&self.pool)
        .await?;

        rows.into_iter().map(map_asset_row).collect()
    }

    pub async fn list_asset_snapshots(
        &self,
        limit: i64,
    ) -> Result<Vec<CatalogAssetSnapshot>, sqlx::Error> {
        let base_query = r#"
            SELECT
                a.asset_id,
                a.capture_year,
                a.capture_month,
                a.capture_day,
                a.camera_make,
                a.camera_model,
                a.lens_model,
                a.iso,
                a.shutter_speed,
                a.aperture,
                a.import_mode,
                d.drive_id,
                a.root_path,
                a.relative_path,
                d.is_online,
                er.recipe_json,
                canonical.recipe_payload AS canonical_recipe_payload,
                canonical.cache_key AS canonical_cache_key,
                canonical.provenance_payload::TEXT AS canonical_provenance_payload,
                TO_CHAR(canonical.updated_at AT TIME ZONE 'UTC',
                        'YYYY-MM-DD"T"HH24:MI:SS.US"Z"')
                    AS canonical_updated_at,
                TO_CHAR(a.created_at AT TIME ZONE 'UTC', 'YYYY-MM-DD"T"HH24:MI:SS"Z"') AS imported_at
            FROM assets a
            INNER JOIN drives d ON d.drive_id = a.drive_id
            LEFT JOIN edit_recipes er ON er.asset_id = a.asset_id
            LEFT JOIN canonical_edit_recipes_v13 canonical
                ON canonical.asset_id = a.asset_id
            ORDER BY a.created_at DESC, a.capture_year DESC, a.capture_month DESC, a.capture_day DESC, a.asset_id ASC
        "#;

        let rows = if limit > 0 {
            sqlx::query(&format!("{base_query}\nLIMIT $1"))
                .bind(limit)
                .fetch_all(&self.pool)
                .await?
        } else {
            sqlx::query(base_query).fetch_all(&self.pool).await?
        };

        rows.into_iter().map(map_asset_snapshot_row).collect()
    }

    pub async fn upsert_edit_recipe(
        &self,
        asset_id: &str,
        recipe_json: &Value,
    ) -> Result<(), sqlx::Error> {
        let mut transaction = self.pool.begin().await?;
        sqlx::query("SELECT pg_advisory_xact_lock(hashtextextended($1, 0))")
            .bind(format!("photo-editor:recipe-migration:{asset_id}"))
            .execute(&mut *transaction)
            .await?;
        let canonical_exists: bool = sqlx::query_scalar(
            "SELECT EXISTS(SELECT 1 FROM canonical_edit_recipes_v13 WHERE asset_id = $1)",
        )
        .bind(asset_id)
        .fetch_one(&mut *transaction)
        .await?;
        if canonical_exists {
            return Err(sqlx::Error::Protocol(
                "legacy save cannot overwrite an asset already migrated to canonical V13".into(),
            ));
        }

        sqlx::query(
            r#"
            INSERT INTO edit_recipes (asset_id, recipe_json, updated_at)
            VALUES ($1, $2, NOW())
            ON CONFLICT (asset_id) DO UPDATE SET
                recipe_json = EXCLUDED.recipe_json,
                updated_at = NOW()
            "#,
        )
        .bind(asset_id)
        .bind(recipe_json)
        .execute(&mut *transaction)
        .await?;
        transaction.commit().await?;

        Ok(())
    }

    pub async fn upsert_canonical_edit_recipe_v13(
        &self,
        asset_id: &str,
        recipe_payload: &str,
        cache_key: &str,
        provenance_payload: &str,
    ) -> Result<(), sqlx::Error> {
        validate_canonical_edit_recipe_v13_input(
            asset_id,
            recipe_payload,
            cache_key,
            provenance_payload,
        )?;
        let mut transaction = self.pool.begin().await?;
        sqlx::query("SELECT pg_advisory_xact_lock(hashtextextended($1, 0))")
            .bind(format!("photo-editor:recipe-migration:{asset_id}"))
            .execute(&mut *transaction)
            .await?;
        sqlx::query(
            r#"
            INSERT INTO canonical_edit_recipes_v13
                (asset_id, recipe_payload, cache_key, provenance_payload, updated_at)
            VALUES ($1, $2, $3, $4::jsonb, NOW())
            ON CONFLICT (asset_id) DO UPDATE SET
                recipe_payload = EXCLUDED.recipe_payload,
                cache_key = EXCLUDED.cache_key,
                provenance_payload = EXCLUDED.provenance_payload,
                updated_at = NOW()
            "#,
        )
        .bind(asset_id)
        .bind(recipe_payload)
        .bind(cache_key)
        .bind(provenance_payload)
        .execute(&mut *transaction)
        .await?;
        transaction.commit().await?;
        Ok(())
    }

    pub async fn canonical_edit_recipe_v13(
        &self,
        asset_id: &str,
    ) -> Result<Option<CanonicalEditRecipeV13>, sqlx::Error> {
        let row = sqlx::query(
            r#"
            SELECT asset_id, recipe_payload, cache_key,
                   provenance_payload::TEXT AS provenance_payload,
                   TO_CHAR(updated_at AT TIME ZONE 'UTC',
                           'YYYY-MM-DD"T"HH24:MI:SS.US"Z"') AS updated_at
            FROM canonical_edit_recipes_v13
            WHERE asset_id = $1
            "#,
        )
        .bind(asset_id)
        .fetch_optional(&self.pool)
        .await?;
        row.map(map_canonical_edit_recipe_row).transpose()
    }

    pub async fn upsert_edit_recipe_compatibility_pair(
        &self,
        asset_id: &str,
        legacy_recipe: &Value,
        canonical_recipe_payload: &str,
        canonical_cache_key: &str,
        canonical_provenance_payload: &str,
    ) -> Result<(), sqlx::Error> {
        validate_canonical_edit_recipe_v13_input(
            asset_id,
            canonical_recipe_payload,
            canonical_cache_key,
            canonical_provenance_payload,
        )?;
        let mut transaction = self.pool.begin().await?;
        sqlx::query("SELECT pg_advisory_xact_lock(hashtextextended($1, 0))")
            .bind(format!("photo-editor:recipe-migration:{asset_id}"))
            .execute(&mut *transaction)
            .await?;

        sqlx::query(
            r#"
            INSERT INTO edit_recipes (asset_id, recipe_json, updated_at)
            VALUES ($1, $2, NOW())
            ON CONFLICT (asset_id) DO UPDATE SET
                recipe_json = EXCLUDED.recipe_json,
                updated_at = NOW()
            "#,
        )
        .bind(asset_id)
        .bind(legacy_recipe)
        .execute(&mut *transaction)
        .await?;
        sqlx::query(
            r#"
            INSERT INTO canonical_edit_recipes_v13
                (asset_id, recipe_payload, cache_key, provenance_payload, updated_at)
            VALUES ($1, $2, $3, $4::jsonb, NOW())
            ON CONFLICT (asset_id) DO UPDATE SET
                recipe_payload = EXCLUDED.recipe_payload,
                cache_key = EXCLUDED.cache_key,
                provenance_payload = EXCLUDED.provenance_payload,
                updated_at = NOW()
            "#,
        )
        .bind(asset_id)
        .bind(canonical_recipe_payload)
        .bind(canonical_cache_key)
        .bind(canonical_provenance_payload)
        .execute(&mut *transaction)
        .await?;
        transaction.commit().await?;
        Ok(())
    }

    pub async fn list_recipe_migration_records(
        &self,
    ) -> Result<Vec<RecipeMigrationRecord>, sqlx::Error> {
        let rows = sqlx::query(
            r#"
            SELECT asset_id, source_recipe_version, source_renderer,
                   target_renderer, profile_identities, plan_hash,
                   evidence_hash,
                   approval_time::TEXT AS approval_time,
                   rollback_pointer, status,
                   created_at::TEXT AS created_at,
                   updated_at::TEXT AS updated_at
            FROM recipe_migration_records
            ORDER BY asset_id
            "#,
        )
        .fetch_all(&self.pool)
        .await?;
        rows.into_iter().map(map_recipe_migration_row).collect()
    }

    pub async fn upsert_recipe_migration_record(
        &self,
        record: &RecipeMigrationRecord,
    ) -> Result<(), sqlx::Error> {
        validate_recipe_migration_record(record)?;
        let profile_identities = serde_json::to_value(&record.profile_identities)
            .map_err(|error| sqlx::Error::Encode(Box::new(error)))?;
        sqlx::query(
            r#"
            INSERT INTO recipe_migration_records (
                asset_id, source_recipe_version, source_renderer,
                target_renderer, profile_identities, plan_hash,
                evidence_hash, approval_time, rollback_pointer,
                status, updated_at)
            VALUES ($1, $2, $3, $4, $5, $6, $7,
                    NULLIF($8, '')::TIMESTAMPTZ, $9, $10, NOW())
            ON CONFLICT (asset_id) DO UPDATE SET
                source_recipe_version = EXCLUDED.source_recipe_version,
                source_renderer = EXCLUDED.source_renderer,
                target_renderer = EXCLUDED.target_renderer,
                profile_identities = EXCLUDED.profile_identities,
                plan_hash = EXCLUDED.plan_hash,
                evidence_hash = EXCLUDED.evidence_hash,
                approval_time = EXCLUDED.approval_time,
                rollback_pointer = EXCLUDED.rollback_pointer,
                status = EXCLUDED.status,
                updated_at = NOW()
            "#,
        )
        .bind(&record.asset_id)
        .bind(record.source_recipe_version)
        .bind(&record.source_renderer)
        .bind(&record.target_renderer)
        .bind(profile_identities)
        .bind(&record.plan_hash)
        .bind(&record.evidence_hash)
        .bind(record.approval_time.as_deref().unwrap_or(""))
        .bind(&record.rollback_pointer)
        .bind(&record.status)
        .execute(&self.pool)
        .await?;
        Ok(())
    }

    pub async fn accept_recipe_migration(
        &self,
        legacy_recipe: &Value,
        canonical_recipe_payload: &str,
        canonical_cache_key: &str,
        canonical_provenance_payload: &str,
        record: &RecipeMigrationRecord,
    ) -> Result<(), sqlx::Error> {
        validate_canonical_edit_recipe_v13_input(
            &record.asset_id,
            canonical_recipe_payload,
            canonical_cache_key,
            canonical_provenance_payload,
        )?;
        validate_recipe_migration_record(record)?;
        let profile_identities = serde_json::to_value(&record.profile_identities)
            .map_err(|error| sqlx::Error::Encode(Box::new(error)))?;
        let mut transaction = self.pool.begin().await?;
        // Serialize acceptance even when this asset has no migration row yet.
        // A helper can commit and lose its acknowledgement when the editor is
        // killed. Replaying that approval must preserve both later ordinary
        // edits and the ORIGINAL pre-migration rollback pointer.
        sqlx::query("SELECT pg_advisory_xact_lock(hashtextextended($1, 0))")
            .bind(format!("photo-editor:recipe-migration:{}", record.asset_id))
            .execute(&mut *transaction)
            .await?;
        if let Some(previous) = sqlx::query(
            r#"
            SELECT source_recipe_version, source_renderer, target_renderer,
                   profile_identities, plan_hash, evidence_hash, status,
                   approval_time = NULLIF($2, '')::TIMESTAMPTZ AS same_approval,
                   approval_time >= NULLIF($2, '')::TIMESTAMPTZ AS newer_approval
            FROM recipe_migration_records WHERE asset_id = $1 FOR UPDATE
            "#,
        )
        .bind(&record.asset_id)
        .bind(record.approval_time.as_deref().unwrap_or(""))
        .fetch_optional(&mut *transaction)
        .await?
        {
            let previous_status = previous.try_get::<String, _>("status")?;
            if previous_status != "candidate"
                && previous.try_get::<String, _>("evidence_hash")? == record.evidence_hash
            {
                let identical = previous_status == "accepted"
                    && record.status == "accepted"
                    && previous.try_get::<Option<bool>, _>("same_approval")? == Some(true)
                    && previous.try_get::<i32, _>("source_recipe_version")?
                        == record.source_recipe_version
                    && previous.try_get::<String, _>("source_renderer")? == record.source_renderer
                    && previous.try_get::<String, _>("target_renderer")? == record.target_renderer
                    && previous.try_get::<Value, _>("profile_identities")? == profile_identities
                    && previous.try_get::<String, _>("plan_hash")? == record.plan_hash;
                if !identical {
                    return Err(sqlx::Error::Protocol(
                        "migration approval was already recorded with different details or rolled back".into(),
                    ));
                }
                transaction.commit().await?;
                return Ok(());
            }
            if previous.try_get::<Option<bool>, _>("newer_approval")? == Some(true) {
                return Err(sqlx::Error::Protocol(
                    "a newer migration decision already exists; stale approval was not replayed"
                        .into(),
                ));
            }
        }
        sqlx::query(
            r#"
            INSERT INTO edit_recipes (asset_id, recipe_json, updated_at)
            VALUES ($1, $2, NOW())
            ON CONFLICT (asset_id) DO UPDATE SET
                recipe_json = EXCLUDED.recipe_json,
                updated_at = NOW()
            "#,
        )
        .bind(&record.asset_id)
        .bind(legacy_recipe)
        .execute(&mut *transaction)
        .await?;
        sqlx::query(
            r#"
            INSERT INTO canonical_edit_recipes_v13
                (asset_id, recipe_payload, cache_key, provenance_payload, updated_at)
            VALUES ($1, $2, $3, $4::jsonb, NOW())
            ON CONFLICT (asset_id) DO UPDATE SET
                recipe_payload = EXCLUDED.recipe_payload,
                cache_key = EXCLUDED.cache_key,
                provenance_payload = EXCLUDED.provenance_payload,
                updated_at = NOW()
            "#,
        )
        .bind(&record.asset_id)
        .bind(canonical_recipe_payload)
        .bind(canonical_cache_key)
        .bind(canonical_provenance_payload)
        .execute(&mut *transaction)
        .await?;
        sqlx::query(
            r#"
            INSERT INTO recipe_migration_records (
                asset_id, source_recipe_version, source_renderer,
                target_renderer, profile_identities, plan_hash,
                evidence_hash, approval_time, rollback_pointer,
                status, updated_at)
            VALUES ($1, $2, $3, $4, $5, $6, $7,
                    NULLIF($8, '')::TIMESTAMPTZ, $9, $10, NOW())
            ON CONFLICT (asset_id) DO UPDATE SET
                source_recipe_version = EXCLUDED.source_recipe_version,
                source_renderer = EXCLUDED.source_renderer,
                target_renderer = EXCLUDED.target_renderer,
                profile_identities = EXCLUDED.profile_identities,
                plan_hash = EXCLUDED.plan_hash,
                evidence_hash = EXCLUDED.evidence_hash,
                approval_time = EXCLUDED.approval_time,
                rollback_pointer = EXCLUDED.rollback_pointer,
                status = EXCLUDED.status,
                updated_at = NOW()
            "#,
        )
        .bind(&record.asset_id)
        .bind(record.source_recipe_version)
        .bind(&record.source_renderer)
        .bind(&record.target_renderer)
        .bind(profile_identities)
        .bind(&record.plan_hash)
        .bind(&record.evidence_hash)
        .bind(record.approval_time.as_deref().unwrap_or(""))
        .bind(&record.rollback_pointer)
        .bind(&record.status)
        .execute(&mut *transaction)
        .await?;
        transaction.commit().await?;
        Ok(())
    }

    pub async fn list_drives(&self) -> Result<Vec<DriveRecord>, sqlx::Error> {
        let rows = sqlx::query(
            r#"
            SELECT drive_id, display_name, mount_path, is_online
            FROM drives
            ORDER BY display_name ASC
            "#,
        )
        .fetch_all(&self.pool)
        .await?;

        rows.into_iter().map(map_drive_row).collect()
    }

    pub async fn list_projects(&self) -> Result<Vec<PersistedProject>, sqlx::Error> {
        let rows = sqlx::query(
            r#"
            SELECT
                p.name,
                TO_CHAR(p.created_at AT TIME ZONE 'UTC', 'YYYY-MM-DD"T"HH24:MI:SS"Z"') AS created_at,
                COALESCE(
                    JSONB_AGG(pa.asset_id ORDER BY pa.asset_id) FILTER (WHERE pa.asset_id IS NOT NULL),
                    '[]'::jsonb
                ) AS asset_ids
            FROM projects p
            LEFT JOIN project_assets pa ON pa.project_name = p.name
            GROUP BY p.name, p.created_at
            ORDER BY p.name ASC
            "#,
        )
        .fetch_all(&self.pool)
        .await?;

        rows.into_iter().map(map_project_row).collect()
    }

    pub async fn upsert_project(&self, name: &str) -> Result<(), sqlx::Error> {
        sqlx::query(
            r#"
            INSERT INTO projects (name, created_at)
            VALUES ($1, NOW())
            ON CONFLICT (name) DO NOTHING
            "#,
        )
        .bind(name)
        .execute(&self.pool)
        .await?;

        Ok(())
    }

    pub async fn add_asset_to_project(
        &self,
        asset_id: &str,
        project_name: &str,
    ) -> Result<(), sqlx::Error> {
        self.upsert_project(project_name).await?;

        sqlx::query(
            r#"
            INSERT INTO project_assets (project_name, asset_id, created_at)
            VALUES ($1, $2, NOW())
            ON CONFLICT (project_name, asset_id) DO NOTHING
            "#,
        )
        .bind(project_name)
        .bind(asset_id)
        .execute(&self.pool)
        .await?;

        Ok(())
    }

    pub async fn remove_asset_from_project(
        &self,
        asset_id: &str,
        project_name: &str,
    ) -> Result<(), sqlx::Error> {
        sqlx::query(
            r#"
            DELETE FROM project_assets
            WHERE project_name = $1 AND asset_id = $2
            "#,
        )
        .bind(project_name)
        .bind(asset_id)
        .execute(&self.pool)
        .await?;

        Ok(())
    }

    pub async fn delete_asset(&self, asset_id: &str) -> Result<(), sqlx::Error> {
        let mut transaction = self.pool.begin().await?;

        sqlx::query(
            r#"
            DELETE FROM edit_recipes
            WHERE asset_id = $1
            "#,
        )
        .bind(asset_id)
        .execute(&mut *transaction)
        .await?;

        sqlx::query(
            r#"
            DELETE FROM timeline_documents
            WHERE asset_id = $1
            "#,
        )
        .bind(asset_id)
        .execute(&mut *transaction)
        .await?;

        sqlx::query(
            r#"
            DELETE FROM assets
            WHERE asset_id = $1
            "#,
        )
        .bind(asset_id)
        .execute(&mut *transaction)
        .await?;

        transaction.commit().await?;
        Ok(())
    }

    pub async fn list_asset_review_states(
        &self,
    ) -> Result<Vec<AssetReviewStateRecord>, sqlx::Error> {
        let rows = sqlx::query(
            r#"
            SELECT asset_id, rating, picked, rejected, color_label
            FROM asset_review_states
            ORDER BY asset_id ASC
            "#,
        )
        .fetch_all(&self.pool)
        .await?;

        rows.into_iter().map(map_asset_review_state_row).collect()
    }

    pub async fn upsert_asset_review_state(
        &self,
        state: &AssetReviewStateRecord,
    ) -> Result<(), sqlx::Error> {
        sqlx::query(
            r#"
            INSERT INTO asset_review_states (asset_id, rating, picked, rejected, color_label, updated_at)
            VALUES ($1, $2, $3, $4, $5, NOW())
            ON CONFLICT (asset_id) DO UPDATE SET
                rating = EXCLUDED.rating,
                picked = EXCLUDED.picked,
                rejected = EXCLUDED.rejected,
                color_label = EXCLUDED.color_label,
                updated_at = NOW()
            "#,
        )
        .bind(&state.asset_id)
        .bind(state.rating)
        .bind(state.picked)
        .bind(state.rejected)
        .bind(&state.color_label)
        .execute(&self.pool)
        .await?;

        Ok(())
    }

    pub async fn list_asset_relinks(&self) -> Result<Vec<AssetRelinkRecord>, sqlx::Error> {
        let rows = sqlx::query(
            r#"
            SELECT asset_id, relinked_file_path
            FROM asset_relinks
            ORDER BY asset_id ASC
            "#,
        )
        .fetch_all(&self.pool)
        .await?;

        rows.into_iter().map(map_asset_relink_row).collect()
    }

    pub async fn set_asset_relink(
        &self,
        asset_id: &str,
        relinked_file_path: &str,
    ) -> Result<(), sqlx::Error> {
        sqlx::query(
            r#"
            INSERT INTO asset_relinks (asset_id, relinked_file_path, updated_at)
            VALUES ($1, $2, NOW())
            ON CONFLICT (asset_id) DO UPDATE SET
                relinked_file_path = EXCLUDED.relinked_file_path,
                updated_at = NOW()
            "#,
        )
        .bind(asset_id)
        .bind(relinked_file_path)
        .execute(&self.pool)
        .await?;

        Ok(())
    }

    pub async fn remove_asset_relink(&self, asset_id: &str) -> Result<(), sqlx::Error> {
        sqlx::query(
            r#"
            DELETE FROM asset_relinks
            WHERE asset_id = $1
            "#,
        )
        .bind(asset_id)
        .execute(&self.pool)
        .await?;

        Ok(())
    }

    pub async fn list_export_jobs(&self) -> Result<Vec<PersistedExportJob>, sqlx::Error> {
        let rows = sqlx::query(
            r#"
            SELECT
                job_id,
                job_kind,
                asset_id,
                media_type,
                display_label,
                source_path,
                edited_preview_source,
                recipe_json,
                metadata_json,
                preset_name,
                destination_path,
                output_path,
                status,
                error_message,
                progress,
                TO_CHAR(created_at AT TIME ZONE 'UTC', 'YYYY-MM-DD"T"HH24:MI:SS"Z"') AS created_at,
                TO_CHAR(updated_at AT TIME ZONE 'UTC', 'YYYY-MM-DD"T"HH24:MI:SS"Z"') AS updated_at
            FROM export_jobs
            ORDER BY created_at ASC, job_id ASC
            "#,
        )
        .fetch_all(&self.pool)
        .await?;

        rows.into_iter().map(map_export_job_row).collect()
    }

    pub async fn upsert_export_job(&self, job: &PersistedExportJob) -> Result<(), sqlx::Error> {
        self.write_export_job(job, true).await?;
        Ok(())
    }

    pub async fn import_legacy_export_job(
        &self,
        job: &PersistedExportJob,
    ) -> Result<bool, sqlx::Error> {
        if self.write_export_job(job, false).await? {
            return Ok(true);
        }
        // A retry after interruption may find the exact payload already stored.
        // A different catalog payload is authoritative and must never be replaced
        // by legacy history. This read only decides whether local cleanup is safe.
        let existing = sqlx::query(
            r#"
            SELECT job_id, job_kind, asset_id, media_type, display_label, source_path,
                edited_preview_source, recipe_json, metadata_json, preset_name,
                destination_path, output_path, status, error_message, progress,
                TO_CHAR(created_at AT TIME ZONE 'UTC', 'YYYY-MM-DD"T"HH24:MI:SS"Z"') AS created_at,
                TO_CHAR(updated_at AT TIME ZONE 'UTC', 'YYYY-MM-DD"T"HH24:MI:SS"Z"') AS updated_at
            FROM export_jobs WHERE job_id = $1
            "#,
        )
        .bind(&job.job_id)
        .fetch_optional(&self.pool)
        .await?
        .map(map_export_job_row)
        .transpose()?;
        Ok(existing.is_some_and(|existing| same_export_job_payload(&existing, job)))
    }

    async fn write_export_job(
        &self,
        job: &PersistedExportJob,
        replace_existing: bool,
    ) -> Result<bool, sqlx::Error> {
        let result = sqlx::query(
            r#"
            INSERT INTO export_jobs (
                job_id,
                job_kind,
                asset_id,
                media_type,
                display_label,
                source_path,
                edited_preview_source,
                recipe_json,
                metadata_json,
                preset_name,
                destination_path,
                output_path,
                status,
                error_message,
                progress,
                asset_ids,
                created_at,
                updated_at
            )
            VALUES (
                $1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14, $15,
                CASE WHEN $3 = '' THEN '[]'::jsonb ELSE to_jsonb(ARRAY[$3]) END,
                NOW(),
                NOW()
            )
            ON CONFLICT (job_id) DO UPDATE SET
                job_kind = EXCLUDED.job_kind,
                asset_id = EXCLUDED.asset_id,
                media_type = EXCLUDED.media_type,
                display_label = EXCLUDED.display_label,
                source_path = EXCLUDED.source_path,
                edited_preview_source = EXCLUDED.edited_preview_source,
                recipe_json = EXCLUDED.recipe_json,
                metadata_json = EXCLUDED.metadata_json,
                preset_name = EXCLUDED.preset_name,
                destination_path = EXCLUDED.destination_path,
                output_path = EXCLUDED.output_path,
                status = EXCLUDED.status,
                error_message = EXCLUDED.error_message,
                progress = EXCLUDED.progress,
                asset_ids = EXCLUDED.asset_ids,
                updated_at = NOW()
            WHERE $16
            "#,
        )
        .bind(&job.job_id)
        .bind(&job.job_kind)
        .bind(&job.asset_id)
        .bind(&job.media_type)
        .bind(&job.display_label)
        .bind(&job.source_path)
        .bind(&job.edited_preview_source)
        .bind(&job.recipe_json)
        .bind(&job.metadata_json)
        .bind(&job.preset_name)
        .bind(&job.destination_path)
        .bind(&job.output_path)
        .bind(&job.status)
        .bind(&job.error_message)
        .bind(job.progress)
        .bind(replace_existing)
        .execute(&self.pool)
        .await?;

        Ok(result.rows_affected() == 1)
    }

    pub async fn delete_export_job(&self, job_id: &str) -> Result<(), sqlx::Error> {
        sqlx::query(
            r#"
            DELETE FROM export_jobs
            WHERE job_id = $1
            "#,
        )
        .bind(job_id)
        .execute(&self.pool)
        .await?;

        Ok(())
    }

    pub async fn list_timeline_documents(
        &self,
    ) -> Result<Vec<PersistedTimelineDocument>, sqlx::Error> {
        let rows = sqlx::query(
            r#"
            SELECT
                timeline_id,
                asset_id,
                name,
                document_path,
                render_path,
                poster_path,
                duration_seconds,
                document_json,
                TO_CHAR(created_at AT TIME ZONE 'UTC', 'YYYY-MM-DD"T"HH24:MI:SS"Z"') AS created_at,
                TO_CHAR(updated_at AT TIME ZONE 'UTC', 'YYYY-MM-DD"T"HH24:MI:SS"Z"') AS updated_at
            FROM timeline_documents
            ORDER BY updated_at DESC, timeline_id ASC
            "#,
        )
        .fetch_all(&self.pool)
        .await?;

        rows.into_iter().map(map_timeline_document_row).collect()
    }

    pub async fn get_timeline_document(
        &self,
        timeline_id: &str,
    ) -> Result<Option<PersistedTimelineDocument>, sqlx::Error> {
        let row = sqlx::query(
            r#"
            SELECT
                timeline_id,
                asset_id,
                name,
                document_path,
                render_path,
                poster_path,
                duration_seconds,
                document_json,
                TO_CHAR(created_at AT TIME ZONE 'UTC', 'YYYY-MM-DD"T"HH24:MI:SS"Z"') AS created_at,
                TO_CHAR(updated_at AT TIME ZONE 'UTC', 'YYYY-MM-DD"T"HH24:MI:SS"Z"') AS updated_at
            FROM timeline_documents
            WHERE timeline_id = $1
            "#,
        )
        .bind(timeline_id)
        .fetch_optional(&self.pool)
        .await?;

        row.map(map_timeline_document_row).transpose()
    }

    pub async fn upsert_timeline_document(
        &self,
        document: &PersistedTimelineDocument,
    ) -> Result<(), sqlx::Error> {
        sqlx::query(
            r#"
            INSERT INTO timeline_documents (
                timeline_id,
                asset_id,
                name,
                document_path,
                render_path,
                poster_path,
                duration_seconds,
                document_json,
                created_at,
                updated_at
            )
            VALUES (
                $1, $2, $3, $4, $5, $6, $7, COALESCE($8, '{}'::jsonb),
                NOW(),
                NOW()
            )
            ON CONFLICT (timeline_id) DO UPDATE SET
                asset_id = EXCLUDED.asset_id,
                name = EXCLUDED.name,
                document_path = EXCLUDED.document_path,
                render_path = EXCLUDED.render_path,
                poster_path = EXCLUDED.poster_path,
                duration_seconds = EXCLUDED.duration_seconds,
                document_json = EXCLUDED.document_json,
                updated_at = NOW()
            "#,
        )
        .bind(&document.timeline_id)
        .bind(&document.asset_id)
        .bind(&document.name)
        .bind(&document.document_path)
        .bind(&document.render_path)
        .bind(&document.poster_path)
        .bind(document.duration_seconds)
        .bind(&document.document_json)
        .execute(&self.pool)
        .await?;

        Ok(())
    }

    pub async fn replace_timeline_documents(
        &self,
        documents: &[PersistedTimelineDocument],
    ) -> Result<(), sqlx::Error> {
        let mut transaction = self.pool.begin().await?;

        sqlx::query(
            r#"
            DELETE FROM timeline_documents
            "#,
        )
        .execute(&mut *transaction)
        .await?;

        for document in documents {
            let created_at = if document.created_at.trim().is_empty() {
                None
            } else {
                Some(document.created_at.as_str())
            };
            let updated_at = if document.updated_at.trim().is_empty() {
                None
            } else {
                Some(document.updated_at.as_str())
            };

            sqlx::query(
                r#"
                INSERT INTO timeline_documents (
                    timeline_id,
                    asset_id,
                    name,
                    document_path,
                    render_path,
                    poster_path,
                    duration_seconds,
                    document_json,
                    created_at,
                    updated_at
                )
                VALUES (
                    $1, $2, $3, $4, $5, $6, $7, COALESCE($8, '{}'::jsonb),
                    COALESCE($9::timestamptz, NOW()),
                    COALESCE($10::timestamptz, NOW())
                )
                "#,
            )
            .bind(&document.timeline_id)
            .bind(&document.asset_id)
            .bind(&document.name)
            .bind(&document.document_path)
            .bind(&document.render_path)
            .bind(&document.poster_path)
            .bind(document.duration_seconds)
            .bind(&document.document_json)
            .bind(created_at)
            .bind(updated_at)
            .execute(&mut *transaction)
            .await?;
        }

        transaction.commit().await?;
        Ok(())
    }

    pub async fn list_carousel_documents(
        &self,
    ) -> Result<Vec<PersistedCarouselDocument>, sqlx::Error> {
        let rows = sqlx::query(
            r#"
            SELECT
                document_id,
                name,
                document_format,
                slide_count,
                document_json,
                TO_CHAR(created_at AT TIME ZONE 'UTC', 'YYYY-MM-DD"T"HH24:MI:SS"Z"') AS created_at,
                TO_CHAR(updated_at AT TIME ZONE 'UTC', 'YYYY-MM-DD"T"HH24:MI:SS"Z"') AS updated_at
            FROM carousel_documents
            ORDER BY updated_at DESC, document_id ASC
            "#,
        )
        .fetch_all(&self.pool)
        .await?;

        rows.into_iter().map(map_carousel_document_row).collect()
    }

    pub async fn get_carousel_document(
        &self,
        document_id: &str,
    ) -> Result<Option<PersistedCarouselDocument>, sqlx::Error> {
        let row = sqlx::query(
            r#"
            SELECT
                document_id,
                name,
                document_format,
                slide_count,
                document_json,
                TO_CHAR(created_at AT TIME ZONE 'UTC', 'YYYY-MM-DD"T"HH24:MI:SS"Z"') AS created_at,
                TO_CHAR(updated_at AT TIME ZONE 'UTC', 'YYYY-MM-DD"T"HH24:MI:SS"Z"') AS updated_at
            FROM carousel_documents
            WHERE document_id = $1
            "#,
        )
        .bind(document_id)
        .fetch_optional(&self.pool)
        .await?;

        row.map(map_carousel_document_row).transpose()
    }

    pub async fn upsert_carousel_document(
        &self,
        document: &PersistedCarouselDocument,
    ) -> Result<(), sqlx::Error> {
        sqlx::query(
            r#"
            INSERT INTO carousel_documents (
                document_id,
                name,
                document_format,
                slide_count,
                document_json,
                created_at,
                updated_at
            )
            VALUES (
                $1, $2, $3, $4, $5,
                NOW(),
                NOW()
            )
            ON CONFLICT (document_id) DO UPDATE SET
                name = EXCLUDED.name,
                document_format = EXCLUDED.document_format,
                slide_count = EXCLUDED.slide_count,
                document_json = EXCLUDED.document_json,
                updated_at = NOW()
            "#,
        )
        .bind(&document.document_id)
        .bind(&document.name)
        .bind(&document.document_format)
        .bind(document.slide_count)
        .bind(&document.document_json)
        .execute(&self.pool)
        .await?;

        Ok(())
    }
}

macro_rules! impl_catalog_repository {
    ($repository:ty) => {
        impl CatalogRepository for $repository {
            async fn healthcheck(&self) -> Result<(), sqlx::Error> {
                <$repository>::healthcheck(self).await
            }

            async fn catalog_integrity_report(
                &self,
            ) -> Result<CatalogIntegrityReport, sqlx::Error> {
                <$repository>::catalog_integrity_report(self).await
            }

            async fn upsert_drive(&self, drive: &DriveRecord) -> Result<(), sqlx::Error> {
                <$repository>::upsert_drive(self, drive).await
            }

            async fn upsert_asset(&self, asset: &CatalogAssetRecord) -> Result<(), sqlx::Error> {
                <$repository>::upsert_asset(self, asset).await
            }

            async fn import_summary(&self, summary: &ImportSummary) -> Result<(), sqlx::Error> {
                <$repository>::import_summary(self, summary).await
            }

            async fn list_assets(
                &self,
                limit: i64,
            ) -> Result<Vec<CatalogAssetRecord>, sqlx::Error> {
                <$repository>::list_assets(self, limit).await
            }

            async fn list_asset_snapshots(
                &self,
                limit: i64,
            ) -> Result<Vec<CatalogAssetSnapshot>, sqlx::Error> {
                <$repository>::list_asset_snapshots(self, limit).await
            }

            async fn upsert_edit_recipe(
                &self,
                asset_id: &str,
                recipe: &Value,
            ) -> Result<(), sqlx::Error> {
                <$repository>::upsert_edit_recipe(self, asset_id, recipe).await
            }

            async fn upsert_canonical_edit_recipe_v13(
                &self,
                asset_id: &str,
                recipe_payload: &str,
                cache_key: &str,
                provenance_payload: &str,
            ) -> Result<(), sqlx::Error> {
                <$repository>::upsert_canonical_edit_recipe_v13(
                    self,
                    asset_id,
                    recipe_payload,
                    cache_key,
                    provenance_payload,
                )
                .await
            }

            async fn canonical_edit_recipe_v13(
                &self,
                asset_id: &str,
            ) -> Result<Option<CanonicalEditRecipeV13>, sqlx::Error> {
                <$repository>::canonical_edit_recipe_v13(self, asset_id).await
            }

            async fn upsert_edit_recipe_compatibility_pair(
                &self,
                asset_id: &str,
                legacy_recipe: &Value,
                canonical_recipe_payload: &str,
                canonical_cache_key: &str,
                canonical_provenance_payload: &str,
            ) -> Result<(), sqlx::Error> {
                <$repository>::upsert_edit_recipe_compatibility_pair(
                    self,
                    asset_id,
                    legacy_recipe,
                    canonical_recipe_payload,
                    canonical_cache_key,
                    canonical_provenance_payload,
                )
                .await
            }

            async fn list_recipe_migration_records(
                &self,
            ) -> Result<Vec<RecipeMigrationRecord>, sqlx::Error> {
                <$repository>::list_recipe_migration_records(self).await
            }

            async fn upsert_recipe_migration_record(
                &self,
                record: &RecipeMigrationRecord,
            ) -> Result<(), sqlx::Error> {
                <$repository>::upsert_recipe_migration_record(self, record).await
            }

            async fn list_drives(&self) -> Result<Vec<DriveRecord>, sqlx::Error> {
                <$repository>::list_drives(self).await
            }

            async fn list_projects(&self) -> Result<Vec<PersistedProject>, sqlx::Error> {
                <$repository>::list_projects(self).await
            }

            async fn upsert_project(&self, name: &str) -> Result<(), sqlx::Error> {
                <$repository>::upsert_project(self, name).await
            }

            async fn add_asset_to_project(
                &self,
                asset_id: &str,
                project_name: &str,
            ) -> Result<(), sqlx::Error> {
                <$repository>::add_asset_to_project(self, asset_id, project_name).await
            }

            async fn remove_asset_from_project(
                &self,
                asset_id: &str,
                project_name: &str,
            ) -> Result<(), sqlx::Error> {
                <$repository>::remove_asset_from_project(self, asset_id, project_name).await
            }

            async fn delete_asset(&self, asset_id: &str) -> Result<(), sqlx::Error> {
                <$repository>::delete_asset(self, asset_id).await
            }

            async fn list_asset_review_states(
                &self,
            ) -> Result<Vec<AssetReviewStateRecord>, sqlx::Error> {
                <$repository>::list_asset_review_states(self).await
            }

            async fn upsert_asset_review_state(
                &self,
                state: &AssetReviewStateRecord,
            ) -> Result<(), sqlx::Error> {
                <$repository>::upsert_asset_review_state(self, state).await
            }

            async fn list_asset_relinks(&self) -> Result<Vec<AssetRelinkRecord>, sqlx::Error> {
                <$repository>::list_asset_relinks(self).await
            }

            async fn set_asset_relink(
                &self,
                asset_id: &str,
                relinked_file_path: &str,
            ) -> Result<(), sqlx::Error> {
                <$repository>::set_asset_relink(self, asset_id, relinked_file_path).await
            }

            async fn remove_asset_relink(&self, asset_id: &str) -> Result<(), sqlx::Error> {
                <$repository>::remove_asset_relink(self, asset_id).await
            }

            async fn list_export_jobs(&self) -> Result<Vec<PersistedExportJob>, sqlx::Error> {
                <$repository>::list_export_jobs(self).await
            }

            async fn upsert_export_job(&self, job: &PersistedExportJob) -> Result<(), sqlx::Error> {
                <$repository>::upsert_export_job(self, job).await
            }

            async fn import_legacy_export_job(
                &self,
                job: &PersistedExportJob,
            ) -> Result<bool, sqlx::Error> {
                <$repository>::import_legacy_export_job(self, job).await
            }

            async fn delete_export_job(&self, job_id: &str) -> Result<(), sqlx::Error> {
                <$repository>::delete_export_job(self, job_id).await
            }

            async fn list_timeline_documents(
                &self,
            ) -> Result<Vec<PersistedTimelineDocument>, sqlx::Error> {
                <$repository>::list_timeline_documents(self).await
            }

            async fn get_timeline_document(
                &self,
                timeline_id: &str,
            ) -> Result<Option<PersistedTimelineDocument>, sqlx::Error> {
                <$repository>::get_timeline_document(self, timeline_id).await
            }

            async fn upsert_timeline_document(
                &self,
                document: &PersistedTimelineDocument,
            ) -> Result<(), sqlx::Error> {
                <$repository>::upsert_timeline_document(self, document).await
            }

            async fn replace_timeline_documents(
                &self,
                documents: &[PersistedTimelineDocument],
            ) -> Result<(), sqlx::Error> {
                <$repository>::replace_timeline_documents(self, documents).await
            }

            async fn list_carousel_documents(
                &self,
            ) -> Result<Vec<PersistedCarouselDocument>, sqlx::Error> {
                <$repository>::list_carousel_documents(self).await
            }

            async fn get_carousel_document(
                &self,
                document_id: &str,
            ) -> Result<Option<PersistedCarouselDocument>, sqlx::Error> {
                <$repository>::get_carousel_document(self, document_id).await
            }

            async fn upsert_carousel_document(
                &self,
                document: &PersistedCarouselDocument,
            ) -> Result<(), sqlx::Error> {
                <$repository>::upsert_carousel_document(self, document).await
            }
        }
    };
}

impl_catalog_repository!(CatalogService);

macro_rules! dispatch_catalog_runtime {
    ($runtime:expr, $method:ident $(, $argument:expr)*) => {
        $runtime.repository.$method($($argument),*).await
    };
}

impl CatalogRepository for CatalogRuntime {
    async fn healthcheck(&self) -> Result<(), sqlx::Error> {
        dispatch_catalog_runtime!(self, healthcheck)
    }

    async fn catalog_integrity_report(&self) -> Result<CatalogIntegrityReport, sqlx::Error> {
        dispatch_catalog_runtime!(self, catalog_integrity_report)
    }

    async fn upsert_drive(&self, drive: &DriveRecord) -> Result<(), sqlx::Error> {
        dispatch_catalog_runtime!(self, upsert_drive, drive)
    }

    async fn upsert_asset(&self, asset: &CatalogAssetRecord) -> Result<(), sqlx::Error> {
        dispatch_catalog_runtime!(self, upsert_asset, asset)
    }

    async fn import_summary(&self, summary: &ImportSummary) -> Result<(), sqlx::Error> {
        dispatch_catalog_runtime!(self, import_summary, summary)
    }

    async fn list_assets(&self, limit: i64) -> Result<Vec<CatalogAssetRecord>, sqlx::Error> {
        dispatch_catalog_runtime!(self, list_assets, limit)
    }

    async fn list_asset_snapshots(
        &self,
        limit: i64,
    ) -> Result<Vec<CatalogAssetSnapshot>, sqlx::Error> {
        dispatch_catalog_runtime!(self, list_asset_snapshots, limit)
    }

    async fn upsert_edit_recipe(&self, asset_id: &str, recipe: &Value) -> Result<(), sqlx::Error> {
        dispatch_catalog_runtime!(self, upsert_edit_recipe, asset_id, recipe)
    }

    async fn upsert_canonical_edit_recipe_v13(
        &self,
        asset_id: &str,
        recipe_payload: &str,
        cache_key: &str,
        provenance_payload: &str,
    ) -> Result<(), sqlx::Error> {
        dispatch_catalog_runtime!(
            self,
            upsert_canonical_edit_recipe_v13,
            asset_id,
            recipe_payload,
            cache_key,
            provenance_payload
        )
    }

    async fn canonical_edit_recipe_v13(
        &self,
        asset_id: &str,
    ) -> Result<Option<CanonicalEditRecipeV13>, sqlx::Error> {
        dispatch_catalog_runtime!(self, canonical_edit_recipe_v13, asset_id)
    }

    async fn upsert_edit_recipe_compatibility_pair(
        &self,
        asset_id: &str,
        legacy_recipe: &Value,
        canonical_recipe_payload: &str,
        canonical_cache_key: &str,
        canonical_provenance_payload: &str,
    ) -> Result<(), sqlx::Error> {
        dispatch_catalog_runtime!(
            self,
            upsert_edit_recipe_compatibility_pair,
            asset_id,
            legacy_recipe,
            canonical_recipe_payload,
            canonical_cache_key,
            canonical_provenance_payload
        )
    }

    async fn list_recipe_migration_records(
        &self,
    ) -> Result<Vec<RecipeMigrationRecord>, sqlx::Error> {
        dispatch_catalog_runtime!(self, list_recipe_migration_records)
    }

    async fn upsert_recipe_migration_record(
        &self,
        record: &RecipeMigrationRecord,
    ) -> Result<(), sqlx::Error> {
        dispatch_catalog_runtime!(self, upsert_recipe_migration_record, record)
    }

    async fn list_drives(&self) -> Result<Vec<DriveRecord>, sqlx::Error> {
        dispatch_catalog_runtime!(self, list_drives)
    }

    async fn list_projects(&self) -> Result<Vec<PersistedProject>, sqlx::Error> {
        dispatch_catalog_runtime!(self, list_projects)
    }

    async fn upsert_project(&self, name: &str) -> Result<(), sqlx::Error> {
        dispatch_catalog_runtime!(self, upsert_project, name)
    }

    async fn add_asset_to_project(
        &self,
        asset_id: &str,
        project_name: &str,
    ) -> Result<(), sqlx::Error> {
        dispatch_catalog_runtime!(self, add_asset_to_project, asset_id, project_name)
    }

    async fn remove_asset_from_project(
        &self,
        asset_id: &str,
        project_name: &str,
    ) -> Result<(), sqlx::Error> {
        dispatch_catalog_runtime!(self, remove_asset_from_project, asset_id, project_name)
    }

    async fn delete_asset(&self, asset_id: &str) -> Result<(), sqlx::Error> {
        dispatch_catalog_runtime!(self, delete_asset, asset_id)
    }

    async fn list_asset_review_states(&self) -> Result<Vec<AssetReviewStateRecord>, sqlx::Error> {
        dispatch_catalog_runtime!(self, list_asset_review_states)
    }

    async fn upsert_asset_review_state(
        &self,
        state: &AssetReviewStateRecord,
    ) -> Result<(), sqlx::Error> {
        dispatch_catalog_runtime!(self, upsert_asset_review_state, state)
    }

    async fn list_asset_relinks(&self) -> Result<Vec<AssetRelinkRecord>, sqlx::Error> {
        dispatch_catalog_runtime!(self, list_asset_relinks)
    }

    async fn set_asset_relink(
        &self,
        asset_id: &str,
        relinked_file_path: &str,
    ) -> Result<(), sqlx::Error> {
        dispatch_catalog_runtime!(self, set_asset_relink, asset_id, relinked_file_path)
    }

    async fn remove_asset_relink(&self, asset_id: &str) -> Result<(), sqlx::Error> {
        dispatch_catalog_runtime!(self, remove_asset_relink, asset_id)
    }

    async fn list_export_jobs(&self) -> Result<Vec<PersistedExportJob>, sqlx::Error> {
        dispatch_catalog_runtime!(self, list_export_jobs)
    }

    async fn upsert_export_job(&self, job: &PersistedExportJob) -> Result<(), sqlx::Error> {
        dispatch_catalog_runtime!(self, upsert_export_job, job)
    }

    async fn import_legacy_export_job(
        &self,
        job: &PersistedExportJob,
    ) -> Result<bool, sqlx::Error> {
        dispatch_catalog_runtime!(self, import_legacy_export_job, job)
    }

    async fn delete_export_job(&self, job_id: &str) -> Result<(), sqlx::Error> {
        dispatch_catalog_runtime!(self, delete_export_job, job_id)
    }

    async fn list_timeline_documents(&self) -> Result<Vec<PersistedTimelineDocument>, sqlx::Error> {
        dispatch_catalog_runtime!(self, list_timeline_documents)
    }

    async fn get_timeline_document(
        &self,
        timeline_id: &str,
    ) -> Result<Option<PersistedTimelineDocument>, sqlx::Error> {
        dispatch_catalog_runtime!(self, get_timeline_document, timeline_id)
    }

    async fn upsert_timeline_document(
        &self,
        document: &PersistedTimelineDocument,
    ) -> Result<(), sqlx::Error> {
        dispatch_catalog_runtime!(self, upsert_timeline_document, document)
    }

    async fn replace_timeline_documents(
        &self,
        documents: &[PersistedTimelineDocument],
    ) -> Result<(), sqlx::Error> {
        dispatch_catalog_runtime!(self, replace_timeline_documents, documents)
    }

    async fn list_carousel_documents(&self) -> Result<Vec<PersistedCarouselDocument>, sqlx::Error> {
        dispatch_catalog_runtime!(self, list_carousel_documents)
    }

    async fn get_carousel_document(
        &self,
        document_id: &str,
    ) -> Result<Option<PersistedCarouselDocument>, sqlx::Error> {
        dispatch_catalog_runtime!(self, get_carousel_document, document_id)
    }

    async fn upsert_carousel_document(
        &self,
        document: &PersistedCarouselDocument,
    ) -> Result<(), sqlx::Error> {
        dispatch_catalog_runtime!(self, upsert_carousel_document, document)
    }
}

pub fn postgres_feature_enabled() -> bool {
    let _ = sqlx::postgres::PgConnectOptions::new();
    true
}

fn default_app_data_dir() -> PathBuf {
    if cfg!(target_os = "macos") {
        return platform_home_dir()
            .unwrap_or_else(current_dir_fallback)
            .join("Library")
            .join("Application Support")
            .join("PhotoEditor");
    }

    if cfg!(target_os = "windows") {
        if let Some(app_data) = std::env::var_os("APPDATA").map(PathBuf::from) {
            return app_data.join("PhotoEditor");
        }

        return platform_home_dir()
            .unwrap_or_else(current_dir_fallback)
            .join("AppData")
            .join("Roaming")
            .join("PhotoEditor");
    }

    if let Some(xdg_data_home) = std::env::var_os("XDG_DATA_HOME").map(PathBuf::from) {
        return xdg_data_home.join("photo-editor");
    }

    platform_home_dir()
        .unwrap_or_else(current_dir_fallback)
        .join(".local")
        .join("share")
        .join("photo-editor")
}

fn platform_home_dir() -> Option<PathBuf> {
    std::env::var_os("HOME")
        .map(PathBuf::from)
        .or_else(|| std::env::var_os("USERPROFILE").map(PathBuf::from))
}

fn current_dir_fallback() -> PathBuf {
    std::env::current_dir().unwrap_or_else(|_| Path::new(".").to_path_buf())
}

fn integrity_error(entity: &str, identity: &str, field: &str, detail: &str) -> sqlx::Error {
    sqlx::Error::Protocol(format!(
        "catalog data integrity error for {entity} '{identity}', field '{field}': {detail}; repair this record or restore a verified catalog backup before retrying"
    ))
}

fn decode_string_array(
    value: &Value,
    entity: &str,
    identity: &str,
    field: &str,
) -> Result<Vec<String>, sqlx::Error> {
    let values = value.as_array().ok_or_else(|| {
        integrity_error(entity, identity, field, "expected a JSON array of strings")
    })?;
    values
        .iter()
        .enumerate()
        .map(|(index, value)| {
            value.as_str().map(str::to_string).ok_or_else(|| {
                integrity_error(
                    entity,
                    identity,
                    field,
                    &format!("element {index} is not a string"),
                )
            })
        })
        .collect()
}

fn validate_capture_date(
    asset_id: &str,
    year: Option<i32>,
    month: Option<i32>,
    day: Option<i32>,
) -> Result<(u16, u8, u8), sqlx::Error> {
    let year =
        year.ok_or_else(|| integrity_error("asset", asset_id, "capture_year", "value is NULL"))?;
    let month = month
        .ok_or_else(|| integrity_error("asset", asset_id, "capture_month", "value is NULL"))?;
    let day =
        day.ok_or_else(|| integrity_error("asset", asset_id, "capture_day", "value is NULL"))?;
    let year = u16::try_from(year).map_err(|_| {
        integrity_error(
            "asset",
            asset_id,
            "capture_year",
            "expected an integer from 1 through 9999",
        )
    })?;
    let month = u8::try_from(month).map_err(|_| {
        integrity_error(
            "asset",
            asset_id,
            "capture_month",
            "expected an integer from 1 through 12",
        )
    })?;
    let day = u8::try_from(day).map_err(|_| {
        integrity_error(
            "asset",
            asset_id,
            "capture_day",
            "expected a valid calendar day",
        )
    })?;
    let leap_year = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    let maximum_day = match month {
        1 | 3 | 5 | 7 | 8 | 10 | 12 => 31,
        4 | 6 | 9 | 11 => 30,
        2 if leap_year => 29,
        2 => 28,
        _ => {
            return Err(integrity_error(
                "asset",
                asset_id,
                "capture_month",
                "expected an integer from 1 through 12",
            ));
        }
    };
    if year == 0 || day == 0 || day > maximum_day {
        return Err(integrity_error(
            "asset",
            asset_id,
            "capture_date",
            "expected a valid Gregorian calendar date",
        ));
    }
    Ok((year, month, day))
}

fn map_canonical_edit_recipe_row(row: PgRow) -> Result<CanonicalEditRecipeV13, sqlx::Error> {
    Ok(CanonicalEditRecipeV13 {
        asset_id: row.try_get("asset_id")?,
        recipe_payload: row.try_get("recipe_payload")?,
        cache_key: row.try_get("cache_key")?,
        provenance_payload: row.try_get("provenance_payload")?,
        updated_at: row.try_get("updated_at")?,
    })
}

fn map_recipe_migration_row(row: PgRow) -> Result<RecipeMigrationRecord, sqlx::Error> {
    let asset_id: String = row.try_get("asset_id")?;
    let profile_identities: Value = row.try_get("profile_identities")?;
    Ok(RecipeMigrationRecord {
        profile_identities: decode_string_array(
            &profile_identities,
            "recipe migration",
            &asset_id,
            "profile_identities",
        )?,
        asset_id,
        source_recipe_version: row.try_get("source_recipe_version")?,
        source_renderer: row.try_get("source_renderer")?,
        target_renderer: row.try_get("target_renderer")?,
        plan_hash: row.try_get("plan_hash")?,
        evidence_hash: row.try_get("evidence_hash")?,
        approval_time: row.try_get("approval_time")?,
        rollback_pointer: row.try_get("rollback_pointer")?,
        status: row.try_get("status")?,
        created_at: row.try_get("created_at")?,
        updated_at: row.try_get("updated_at")?,
    })
}

fn map_drive_row(row: PgRow) -> Result<DriveRecord, sqlx::Error> {
    Ok(DriveRecord {
        drive_id: row.try_get("drive_id")?,
        display_name: row.try_get("display_name")?,
        mount_path: row.try_get("mount_path")?,
        online: row.try_get("is_online")?,
    })
}

fn map_project_row(row: PgRow) -> Result<PersistedProject, sqlx::Error> {
    let name: String = row.try_get("name")?;
    let asset_ids: Value = row.try_get("asset_ids")?;
    Ok(PersistedProject {
        asset_ids: decode_string_array(&asset_ids, "project", &name, "asset_ids")?,
        name,
        created_at: row.try_get("created_at")?,
    })
}

fn map_asset_review_state_row(row: PgRow) -> Result<AssetReviewStateRecord, sqlx::Error> {
    let asset_id: String = row.try_get("asset_id")?;
    let rating: i32 = row.try_get("rating")?;
    let picked: bool = row.try_get("picked")?;
    let rejected: bool = row.try_get("rejected")?;
    if !(0..=5).contains(&rating) {
        return Err(integrity_error(
            "asset review state",
            &asset_id,
            "rating",
            "expected an integer from 0 through 5",
        ));
    }
    if picked && rejected {
        return Err(integrity_error(
            "asset review state",
            &asset_id,
            "picked/rejected",
            "an asset cannot be both picked and rejected",
        ));
    }
    Ok(AssetReviewStateRecord {
        asset_id,
        rating,
        picked,
        rejected,
        color_label: row.try_get("color_label")?,
    })
}

fn map_asset_relink_row(row: PgRow) -> Result<AssetRelinkRecord, sqlx::Error> {
    let asset_id: String = row.try_get("asset_id")?;
    let relinked_file_path = row
        .try_get::<Option<String>, _>("relinked_file_path")?
        .ok_or_else(|| {
            integrity_error(
                "asset relink",
                &asset_id,
                "relinked_file_path",
                "value is NULL",
            )
        })?;
    Ok(AssetRelinkRecord {
        asset_id,
        relinked_file_path,
    })
}

fn same_export_job_payload(left: &PersistedExportJob, right: &PersistedExportJob) -> bool {
    let mut left = left.clone();
    let mut right = right.clone();
    // Catalog timestamps are assigned by PostgreSQL, not imported from legacy
    // strings. All actual job fields, including status and metadata, must match.
    left.created_at.clear();
    left.updated_at.clear();
    right.created_at.clear();
    right.updated_at.clear();
    left == right
}

fn map_export_job_row(row: PgRow) -> Result<PersistedExportJob, sqlx::Error> {
    let job_id: String = row.try_get("job_id")?;
    let progress: i32 = row.try_get("progress")?;
    if !(0..=100).contains(&progress) {
        return Err(integrity_error(
            "export job",
            &job_id,
            "progress",
            "expected an integer from 0 through 100",
        ));
    }
    Ok(PersistedExportJob {
        job_id,
        job_kind: row.try_get("job_kind")?,
        asset_id: row.try_get("asset_id")?,
        media_type: row.try_get("media_type")?,
        display_label: row.try_get("display_label")?,
        source_path: row.try_get("source_path")?,
        edited_preview_source: row.try_get("edited_preview_source")?,
        recipe_json: row.try_get("recipe_json")?,
        metadata_json: row.try_get("metadata_json")?,
        preset_name: row.try_get("preset_name")?,
        destination_path: row.try_get("destination_path")?,
        output_path: row.try_get("output_path")?,
        status: row.try_get("status")?,
        error_message: row.try_get("error_message")?,
        progress,
        created_at: row.try_get("created_at")?,
        updated_at: row.try_get("updated_at")?,
    })
}

fn map_timeline_document_row(row: PgRow) -> Result<PersistedTimelineDocument, sqlx::Error> {
    let timeline_id: String = row.try_get("timeline_id")?;
    let duration_seconds: f64 = row.try_get("duration_seconds")?;
    if !duration_seconds.is_finite() || duration_seconds < 0.0 {
        return Err(integrity_error(
            "timeline document",
            &timeline_id,
            "duration_seconds",
            "expected a finite non-negative number",
        ));
    }
    Ok(PersistedTimelineDocument {
        timeline_id,
        asset_id: row.try_get("asset_id")?,
        name: row.try_get("name")?,
        document_path: row.try_get("document_path")?,
        render_path: row.try_get("render_path")?,
        poster_path: row.try_get("poster_path")?,
        duration_seconds,
        document_json: row.try_get("document_json")?,
        created_at: row.try_get("created_at")?,
        updated_at: row.try_get("updated_at")?,
    })
}

fn map_carousel_document_row(row: PgRow) -> Result<PersistedCarouselDocument, sqlx::Error> {
    let document_id: String = row.try_get("document_id")?;
    let slide_count: i32 = row.try_get("slide_count")?;
    if slide_count < 0 {
        return Err(integrity_error(
            "carousel document",
            &document_id,
            "slide_count",
            "expected a non-negative integer",
        ));
    }
    Ok(PersistedCarouselDocument {
        document_id,
        name: row.try_get("name")?,
        document_format: row.try_get("document_format")?,
        slide_count,
        document_json: row.try_get("document_json")?,
        created_at: row.try_get("created_at")?,
        updated_at: row.try_get("updated_at")?,
    })
}

fn map_asset_row(row: PgRow) -> Result<CatalogAssetRecord, sqlx::Error> {
    let asset_id: String = row.try_get("asset_id")?;
    let capture_year: Option<i32> = row.try_get("capture_year")?;
    let capture_month: Option<i32> = row.try_get("capture_month")?;
    let capture_day: Option<i32> = row.try_get("capture_day")?;
    let (capture_year, capture_month, capture_day) =
        validate_capture_date(&asset_id, capture_year, capture_month, capture_day)?;
    let import_mode: String = row.try_get("import_mode")?;
    let iso: Option<i32> = row.try_get("iso")?;
    if iso.is_some_and(|value| value <= 0) {
        return Err(integrity_error(
            "asset",
            &asset_id,
            "iso",
            "expected a positive integer or NULL",
        ));
    }
    let aperture: Option<f64> = row.try_get("aperture")?;
    if aperture.is_some_and(|value| !value.is_finite() || value <= 0.0) {
        return Err(integrity_error(
            "asset",
            &asset_id,
            "aperture",
            "expected a finite positive number or NULL",
        ));
    }
    let import_mode = ImportMode::from_database_value(&import_mode).map_err(|_| {
        integrity_error(
            "asset",
            &asset_id,
            "import_mode",
            &format!("unknown value '{import_mode}'"),
        )
    })?;

    Ok(CatalogAssetRecord {
        asset_id,
        capture_year,
        capture_month,
        capture_day,
        camera_make: row
            .try_get::<Option<String>, _>("camera_make")?
            .unwrap_or_default(),
        camera_model: row
            .try_get::<Option<String>, _>("camera_model")?
            .unwrap_or_default(),
        lens_model: row
            .try_get::<Option<String>, _>("lens_model")?
            .unwrap_or_default(),
        iso,
        shutter_speed: row
            .try_get::<Option<String>, _>("shutter_speed")?
            .unwrap_or_default(),
        aperture,
        import_mode,
        location: AssetLocation {
            drive_id: row.try_get("drive_id")?,
            root_path: row.try_get("root_path")?,
            relative_path: row.try_get("relative_path")?,
            online: row.try_get("is_online")?,
        },
    })
}

fn map_asset_snapshot_row(row: PgRow) -> Result<CatalogAssetSnapshot, sqlx::Error> {
    let asset_id: String = row.try_get("asset_id")?;
    let edit_recipe: Option<Value> = row.try_get("recipe_json")?;
    let canonical_recipe_payload: Option<String> = row.try_get("canonical_recipe_payload")?;
    let canonical_edit_recipe_v13 = match canonical_recipe_payload {
        Some(recipe_payload) => Some(CanonicalEditRecipeV13 {
            asset_id: asset_id.clone(),
            recipe_payload,
            cache_key: row.try_get("canonical_cache_key")?,
            provenance_payload: row.try_get("canonical_provenance_payload")?,
            updated_at: row.try_get("canonical_updated_at")?,
        }),
        None => None,
    };
    let imported_at: String = row.try_get("imported_at")?;

    Ok(CatalogAssetSnapshot {
        asset: map_asset_row(row)?,
        edit_recipe,
        canonical_edit_recipe_v13,
        imported_at,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn legacy_job_retry_requires_identical_payload_not_timestamps() {
        let job = PersistedExportJob {
            job_id: "legacy".into(),
            job_kind: "asset_export".into(),
            asset_id: "photo".into(),
            media_type: "photo".into(),
            display_label: "Photo".into(),
            source_path: "/source.jpg".into(),
            edited_preview_source: String::new(),
            recipe_json: Some(serde_json::json!({"exposure": 0.5})),
            metadata_json: Some(serde_json::json!({"pauseRequested": true})),
            preset_name: "Print".into(),
            destination_path: "/delivery".into(),
            output_path: "/delivery/photo.png".into(),
            status: "Paused".into(),
            error_message: String::new(),
            progress: 50,
            created_at: String::new(),
            updated_at: String::new(),
        };
        let mut stored = job.clone();
        stored.created_at = "2026-09-08T00:00:00Z".into();
        stored.updated_at = "2026-09-08T00:00:01Z".into();
        assert!(same_export_job_payload(&job, &stored));
        stored.status = "Canceled".into();
        assert!(!same_export_job_payload(&job, &stored));
        stored = job.clone();
        stored.metadata_json = Some(serde_json::json!({"pauseRequested": false}));
        assert!(!same_export_job_payload(&job, &stored));
        stored = job.clone();
        stored.recipe_json = Some(serde_json::json!({"exposure": 1.0}));
        assert!(!same_export_job_payload(&job, &stored));
        stored = job.clone();
        stored.output_path = "/other.png".into();
        assert!(!same_export_job_payload(&job, &stored));
        stored = job.clone();
        stored.progress = 100;
        assert!(!same_export_job_payload(&job, &stored));
    }

    #[test]
    fn canonical_provenance_preserves_versioned_history() {
        let valid = serde_json::json!({
            "schema": "photo-editor-edit-recipe-provenance:v2",
            "presetApplications": [],
            "history": {"activeBranchId": "review", "snapshots": [{"id": "review-1"}]}
        });
        let check = |value: &Value| {
            validate_canonical_edit_recipe_v13_input("asset", "recipe", "cache", &value.to_string())
        };
        assert!(check(&valid).is_ok());
        assert!(validate_canonical_edit_recipe_v13_input(
            "asset",
            "recipe",
            "cache",
            EMPTY_CANONICAL_RECIPE_PROVENANCE_V1
        )
        .is_ok());
        for invalid_history in [
            Value::Null,
            serde_json::json!({}),
            serde_json::json!({"activeBranchId": "", "snapshots": []}),
            serde_json::json!({"activeBranchId": "main", "snapshots": {}}),
            serde_json::json!({"activeBranchId": "main", "snapshots": [], "extra": 1}),
        ] {
            let mut invalid = valid.clone();
            invalid["history"] = invalid_history;
            assert!(check(&invalid).is_err());
        }
        let mut extra = valid.clone();
        extra["unknown"] = Value::Bool(true);
        assert!(check(&extra).is_err());
    }

    #[test]
    fn config_uses_defaults() {
        let config = PostgresCatalogConfig::default();
        assert_eq!(config.database_url, DEFAULT_DATABASE_URL);
        assert_eq!(config.max_connections, DEFAULT_MAX_CONNECTIONS);
    }

    #[test]
    fn import_mode_round_trips_database_values() {
        assert_eq!(ImportMode::Managed.as_database_value(), "managed");
        assert_eq!(ImportMode::Referenced.as_database_value(), "referenced");
        assert_eq!(
            ImportMode::from_database_value("managed").unwrap(),
            ImportMode::Managed
        );
        assert_eq!(
            ImportMode::from_database_value("referenced").unwrap(),
            ImportMode::Referenced
        );
        assert!(ImportMode::from_database_value("anything-else").is_err());
    }

    #[test]
    fn capture_date_validation_rejects_null_and_impossible_dates() {
        assert_eq!(
            validate_capture_date("asset-1", Some(2024), Some(2), Some(29)).unwrap(),
            (2024, 2, 29)
        );
        let null_error = validate_capture_date("asset-1", None, Some(2), Some(1))
            .unwrap_err()
            .to_string();
        assert!(null_error.contains("asset-1"));
        assert!(null_error.contains("capture_year"));
        assert!(validate_capture_date("asset-2", Some(2023), Some(2), Some(29)).is_err());
        assert!(validate_capture_date("asset-3", Some(2024), Some(13), Some(1)).is_err());
    }

    #[test]
    fn string_array_decoding_rejects_shape_and_element_drift() {
        assert_eq!(
            decode_string_array(
                &serde_json::json!(["one", "two"]),
                "project",
                "Favorites",
                "asset_ids"
            )
            .unwrap(),
            vec!["one", "two"]
        );
        let shape_error =
            decode_string_array(&serde_json::json!({}), "project", "Favorites", "asset_ids")
                .unwrap_err()
                .to_string();
        assert!(shape_error.contains("Favorites"));
        assert!(shape_error.contains("asset_ids"));
        assert!(decode_string_array(
            &serde_json::json!(["one", 2]),
            "project",
            "Favorites",
            "asset_ids"
        )
        .is_err());
    }

    #[test]
    fn helper_diagnostics_redact_postgres_credentials() {
        assert_eq!(
            sanitize_catalog_diagnostic(
                "connect postgresql://photo_editor:super-secret@127.0.0.1:5433/photo_editor failed"
            ),
            "connect postgresql://photo_editor:<redacted>@127.0.0.1:5433/photo_editor failed"
        );
        assert_eq!(
            sanitize_catalog_diagnostic("catalog data integrity error for asset 'a'"),
            "catalog data integrity error for asset 'a'"
        );
    }

    #[tokio::test]
    async fn postgres_feature_probe_is_available() {
        assert!(postgres_feature_enabled());
    }

    #[test]
    fn runtime_paths_default_to_app_relative_subdirectories() {
        let app_data_dir = PathBuf::from("/tmp/photo-editor-data");
        let paths = AppRuntimePaths {
            app_data_dir: app_data_dir.clone(),
            log_dir: app_data_dir.join("logs"),
            backup_dir: app_data_dir.join("backups"),
        };

        assert_eq!(paths.log_dir, app_data_dir.join("logs"));
        assert_eq!(paths.backup_dir, app_data_dir.join("backups"));
    }
}
