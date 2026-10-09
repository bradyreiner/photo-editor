pub mod helper_arguments;
mod asset_metadata;
pub use asset_metadata::{AssetMetadataRecord, validate_asset_metadata};
mod importer;
mod local_database;
pub mod observability;

use std::fs;
use std::future::Future;
use std::io::Read;
use std::panic::{self, AssertUnwindSafe};
use std::path::{Path, PathBuf};
use std::process::ExitCode;

use futures_util::TryStreamExt;
use serde::{Deserialize, Serialize};
use serde_json::Value;
use sha2::{Digest, Sha256};
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

const ASSET_SNAPSHOT_QUERY: &str = r#"
    SELECT
        a.asset_id,
        a.capture_year,
        a.capture_month,
        a.capture_day,
        a.capture_timestamp,
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

const AGENT_ASSET_RECIPE_SNAPSHOT_QUERY: &str = r#"
    SELECT
        a.asset_id,
        a.root_path,
        a.relative_path,
        er.recipe_json,
        canonical.recipe_payload AS canonical_recipe_payload,
        canonical.cache_key AS canonical_cache_key,
        canonical.provenance_payload::TEXT AS canonical_provenance_payload,
        jsonb_build_object(
            'rating', a.rating,
            'flagState', a.flag_state,
            'colorLabel', a.color_label,
            'importMode', a.import_mode,
            'updatedAt', TO_CHAR(a.updated_at AT TIME ZONE 'UTC', 'YYYY-MM-DD"T"HH24:MI:SS.US"Z"'),
            'projects', COALESCE((
                SELECT jsonb_agg(pa.project_name ORDER BY pa.project_name)
                FROM project_assets pa WHERE pa.asset_id = a.asset_id
            ), '[]'::jsonb),
            'review', (SELECT jsonb_build_object(
                'rating', rs.rating, 'picked', rs.picked, 'rejected', rs.rejected,
                'colorLabel', rs.color_label
            ) FROM asset_review_states rs WHERE rs.asset_id = a.asset_id),
            'metadata', (SELECT am.metadata_json FROM asset_metadata am WHERE am.asset_id = a.asset_id),
            'relinkPath', (SELECT ar.relinked_file_path FROM asset_relinks ar WHERE ar.asset_id = a.asset_id),
            'timelineDocuments', COALESCE((
                SELECT jsonb_agg(jsonb_build_object(
                    'timelineId', td.timeline_id, 'name', td.name,
                    'documentPath', td.document_path, 'renderPath', td.render_path,
                    'posterPath', td.poster_path, 'document', td.document_json
                ) ORDER BY td.timeline_id)
                FROM timeline_documents td WHERE td.asset_id = a.asset_id
            ), '[]'::jsonb)
        ) AS catalog_row_state
    FROM assets a
    INNER JOIN drives d ON d.drive_id = a.drive_id
    LEFT JOIN edit_recipes er ON er.asset_id = a.asset_id
    LEFT JOIN canonical_edit_recipes_v13 canonical ON canonical.asset_id = a.asset_id
    WHERE ($1 <> '' AND a.asset_id = $1)
       OR ($2 <> '' AND
           (CASE WHEN RIGHT(a.root_path, 1) = '/' THEN a.root_path || a.relative_path
                 ELSE a.root_path || '/' || a.relative_path END) = $2)
    ORDER BY a.asset_id ASC
    LIMIT 2
"#;

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
    #[serde(default)]
    pub capture_timestamp: Option<String>,
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

/// Exact catalog and source identity used by the desktop agent's per-asset
/// recipe journal. Timestamps that change on a successful restore are
/// intentionally excluded; canonical recipe payload fields remain exact.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
#[serde(deny_unknown_fields)]
pub struct AgentAssetRecipeSnapshot {
    pub schema: String,
    pub state: String,
    pub asset_id: String,
    pub source_path: String,
    pub source_sha256: String,
    pub legacy_recipe: Option<Value>,
    pub canonical_recipe: Option<AgentCanonicalRecipe>,
    pub catalog_row_state: Value,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
#[serde(deny_unknown_fields)]
pub struct AgentCatalogRowSnapshot {
    pub schema: String,
    pub state: String,
    pub asset_id: String,
    pub source_path: String,
    pub source_sha256: String,
    pub catalog_row_state: Value,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
#[serde(deny_unknown_fields)]
pub struct AgentCanonicalRecipe {
    pub recipe_payload: String,
    pub cache_key: String,
    pub provenance_payload: String,
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
pub struct ReviewStateBatchResult {
    pub applied: Vec<String>,
    pub missing: Vec<String>,
}

pub fn validate_review_state_batch(states: &[AssetReviewStateRecord]) -> Result<(), &'static str> {
    if states.is_empty() || states.len() > 256 {
        return Err("review batch must contain 1 to 256 records");
    }
    let mut ids = std::collections::HashSet::new();
    for state in states {
        if state.asset_id.trim().is_empty()
            || state.asset_id != state.asset_id.trim()
            || !ids.insert(&state.asset_id)
            || !(0..=5).contains(&state.rating)
            || (state.picked && state.rejected)
            || !["", "red", "yellow", "green", "blue", "purple"]
                .contains(&state.color_label.as_str())
        {
            return Err("invalid or duplicate review state");
        }
    }
    Ok(())
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
    async fn update_project_memberships(
        &self,
        project_name: &str,
        added: &[String],
        removed: &[String],
    ) -> Result<Vec<String>, sqlx::Error>;
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
    async fn upsert_asset_review_states(
        &self,
        states: &[AssetReviewStateRecord],
    ) -> Result<ReviewStateBatchResult, sqlx::Error>;
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

    /// Stream the same complete, ordered asset array used by catalog snapshots.
    /// A caller must discard partial output if this operation returns an error.
    pub async fn write_asset_snapshots_json<W: std::io::Write>(
        &self,
        limit: i64,
        writer: &mut W,
    ) -> Result<(), Box<dyn std::error::Error>> {
        self.repository
            .write_asset_snapshots_json(limit, writer)
            .await
    }

    pub fn backend_name(&self) -> &'static str {
        "postgresql"
    }

    pub async fn agent_asset_recipe_snapshot(
        &self,
        asset_id: &str,
        source_path: &str,
    ) -> Result<AgentAssetRecipeSnapshot, sqlx::Error> {
        self.repository
            .agent_asset_recipe_snapshot(asset_id, source_path)
            .await
    }

    pub async fn save_agent_edit_recipe_if_unchanged(
        &self,
        asset_id: &str,
        expected: &AgentAssetRecipeSnapshot,
        legacy_recipe: &Value,
        canonical_recipe: Option<&AgentCanonicalRecipe>,
    ) -> Result<(), sqlx::Error> {
        self.repository
            .save_agent_edit_recipe_if_unchanged(
                asset_id,
                expected,
                legacy_recipe,
                canonical_recipe,
            )
            .await
    }

    pub async fn agent_catalog_row_snapshot(
        &self,
        source_path: &str,
    ) -> Result<AgentCatalogRowSnapshot, sqlx::Error> {
        self.repository
            .agent_catalog_row_snapshot(source_path)
            .await
    }

    pub async fn restore_agent_asset_recipe(
        &self,
        expected: &AgentAssetRecipeSnapshot,
        before: &AgentAssetRecipeSnapshot,
    ) -> Result<(), sqlx::Error> {
        self.repository
            .restore_agent_asset_recipe(expected, before)
            .await
    }

    pub async fn remove_agent_catalog_row_if_unchanged(
        &self,
        expected: &AgentCatalogRowSnapshot,
    ) -> Result<(), sqlx::Error> {
        self.repository
            .remove_agent_catalog_row_if_unchanged(expected)
            .await
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

    pub async fn accept_agent_recipe_migration_if_unchanged(
        &self,
        expected: &AgentAssetRecipeSnapshot,
        legacy_recipe: &Value,
        canonical_recipe_payload: &str,
        canonical_cache_key: &str,
        canonical_provenance_payload: &str,
        record: &RecipeMigrationRecord,
    ) -> Result<(), sqlx::Error> {
        self.repository
            .accept_agent_recipe_migration_if_unchanged(
                expected,
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
                capture_timestamp,
                camera_make,
                camera_model,
                lens_model,
                iso,
                shutter_speed,
                aperture,
                created_at,
                updated_at
            )
            VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14, $15, NOW(), NOW())
            ON CONFLICT (asset_id) DO UPDATE SET
                drive_id = EXCLUDED.drive_id,
                root_path = EXCLUDED.root_path,
                relative_path = EXCLUDED.relative_path,
                import_mode = EXCLUDED.import_mode,
                capture_year = EXCLUDED.capture_year,
                capture_month = EXCLUDED.capture_month,
                capture_day = EXCLUDED.capture_day,
                capture_timestamp = EXCLUDED.capture_timestamp,
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
        .bind(&asset.capture_timestamp)
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
                    capture_timestamp,
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
                    .push_bind(&asset.capture_timestamp)
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
                    capture_timestamp = EXCLUDED.capture_timestamp,
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
                a.capture_timestamp,
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
        let base_query = ASSET_SNAPSHOT_QUERY;

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

    async fn write_asset_snapshots_json<W: std::io::Write>(
        &self,
        limit: i64,
        writer: &mut W,
    ) -> Result<(), Box<dyn std::error::Error>> {
        let query_text = if limit > 0 {
            format!("{ASSET_SNAPSHOT_QUERY}\nLIMIT $1")
        } else {
            ASSET_SNAPSHOT_QUERY.to_owned()
        };
        let mut query = sqlx::query(&query_text);
        if limit > 0 {
            query = query.bind(limit);
        }
        // One SELECT preserves ordering and statement-level consistency without
        // retaining the full PgRow collection or converted asset vector.
        let mut rows = query.fetch(&self.pool);
        writer.write_all(b"[")?;
        let mut first = true;
        while let Some(row) = rows.try_next().await? {
            let asset = map_asset_snapshot_row(row)?;
            if !first {
                writer.write_all(b",")?;
            }
            serde_json::to_writer(&mut *writer, &asset)?;
            first = false;
        }
        writer.write_all(b"]")?;
        Ok(())
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

    /// Save an agent-owned recipe only if the exact source, catalog metadata,
    /// and prior recipe snapshot captured before launch still match under the
    /// same advisory lock used by all recipe writers.
    pub async fn save_agent_edit_recipe_if_unchanged(
        &self,
        asset_id: &str,
        expected: &AgentAssetRecipeSnapshot,
        legacy_recipe: &Value,
        canonical_recipe: Option<&AgentCanonicalRecipe>,
    ) -> Result<(), sqlx::Error> {
        validate_agent_task_recipe_snapshot(expected, asset_id)?;
        if let Some(canonical) = canonical_recipe {
            validate_canonical_edit_recipe_v13_input(
                &expected.asset_id,
                &canonical.recipe_payload,
                &canonical.cache_key,
                &canonical.provenance_payload,
            )?;
        } else if expected.canonical_recipe.is_some() {
            return Err(agent_state_error(
                "legacy-only owned save cannot replace a canonical recipe",
            ));
        }

        let mut transaction = self.pool.begin().await?;
        sqlx::query("SELECT pg_advisory_xact_lock(hashtextextended($1, 0))")
            .bind(format!(
                "photo-editor:recipe-migration:{}",
                expected.asset_id
            ))
            .execute(&mut *transaction)
            .await?;
        self.lock_and_validate_agent_task_recipe_snapshot(&mut transaction, expected)
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
        .bind(&expected.asset_id)
        .bind(legacy_recipe)
        .execute(&mut *transaction)
        .await?;
        if let Some(canonical) = canonical_recipe {
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
            .bind(&expected.asset_id)
            .bind(&canonical.recipe_payload)
            .bind(&canonical.cache_key)
            .bind(&canonical.provenance_payload)
            .execute(&mut *transaction)
            .await?;
        }
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
        self.accept_recipe_migration_with_snapshot(
            None,
            legacy_recipe,
            canonical_recipe_payload,
            canonical_cache_key,
            canonical_provenance_payload,
            record,
        )
        .await
    }

    pub async fn accept_agent_recipe_migration_if_unchanged(
        &self,
        expected: &AgentAssetRecipeSnapshot,
        legacy_recipe: &Value,
        canonical_recipe_payload: &str,
        canonical_cache_key: &str,
        canonical_provenance_payload: &str,
        record: &RecipeMigrationRecord,
    ) -> Result<(), sqlx::Error> {
        self.accept_recipe_migration_with_snapshot(
            Some(expected),
            legacy_recipe,
            canonical_recipe_payload,
            canonical_cache_key,
            canonical_provenance_payload,
            record,
        )
        .await
    }

    async fn accept_recipe_migration_with_snapshot(
        &self,
        expected: Option<&AgentAssetRecipeSnapshot>,
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
        if let Some(expected) = expected {
            if expected.asset_id != record.asset_id {
                return Err(agent_state_error(
                    "owned migration snapshot does not match the target asset",
                ));
            }
            self.lock_and_validate_agent_task_recipe_snapshot(&mut transaction, expected)
                .await?;
        }
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

    pub async fn update_project_memberships(
        &self,
        project_name: &str,
        added: &[String],
        removed: &[String],
    ) -> Result<Vec<String>, sqlx::Error> {
        let mut transaction = self.pool.begin().await?;
        sqlx::query("INSERT INTO projects (name, created_at) VALUES ($1, NOW()) ON CONFLICT (name) DO NOTHING")
            .bind(project_name)
            .execute(&mut *transaction)
            .await?;
        sqlx::query(
            "DELETE FROM project_assets WHERE project_name = $1 AND asset_id = ANY($2::text[])",
        )
        .bind(project_name)
        .bind(removed)
        .execute(&mut *transaction)
        .await?;
        // Virtual copies and assets deleted since selection may have no catalog
        // row. Persist valid members together and report the local-only remainder.
        let missing = sqlx::query_scalar::<_, String>(
            "SELECT id FROM UNNEST($1::text[]) AS requested(id) WHERE NOT EXISTS (SELECT 1 FROM assets WHERE asset_id = id)",
        )
        .bind(added)
        .fetch_all(&mut *transaction)
        .await?;
        sqlx::query(
            "INSERT INTO project_assets (project_name, asset_id, created_at) SELECT $1, asset_id, NOW() FROM assets WHERE asset_id = ANY($2::text[]) ON CONFLICT (project_name, asset_id) DO NOTHING",
        )
        .bind(project_name)
        .bind(added)
        .execute(&mut *transaction)
        .await?;
        transaction.commit().await?;
        Ok(missing)
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

    pub async fn agent_asset_recipe_snapshot(
        &self,
        asset_id: &str,
        source_path: &str,
    ) -> Result<AgentAssetRecipeSnapshot, sqlx::Error> {
        if asset_id.trim().is_empty() && source_path.trim().is_empty() {
            return Err(agent_state_error(
                "agent asset snapshot requires an asset ID or source path",
            ));
        }
        let rows = sqlx::query(AGENT_ASSET_RECIPE_SNAPSHOT_QUERY)
            .bind(asset_id)
            .bind(source_path)
            .fetch_all(&self.pool)
            .await?;
        if rows.len() > 1 {
            return Err(agent_state_error(
                "agent asset snapshot matched more than one catalog row",
            ));
        }
        let Some(row) = rows.into_iter().next() else {
            return Ok(AgentAssetRecipeSnapshot {
                schema: "agent-asset-recipe:v1".into(),
                state: "absent".into(),
                asset_id: String::new(),
                source_path: source_path.to_string(),
                source_sha256: String::new(),
                legacy_recipe: None,
                canonical_recipe: None,
                catalog_row_state: serde_json::json!({}),
            });
        };
        agent_recipe_snapshot_from_row(row, asset_id, source_path)
    }

    async fn lock_and_validate_agent_task_recipe_snapshot(
        &self,
        transaction: &mut sqlx::Transaction<'_, Postgres>,
        expected: &AgentAssetRecipeSnapshot,
    ) -> Result<(), sqlx::Error> {
        validate_agent_task_recipe_snapshot(expected, &expected.asset_id)?;
        let locked = sqlx::query(
            r#"SELECT a.asset_id
               FROM assets a
               INNER JOIN drives d ON d.drive_id = a.drive_id
               WHERE a.asset_id = $1
               FOR UPDATE OF a, d"#,
        )
        .bind(&expected.asset_id)
        .fetch_optional(&mut **transaction)
        .await?;
        if locked.is_none() {
            return Err(agent_state_error(
                "agent recipe asset row is missing; owned write was not applied",
            ));
        }
        let rows = sqlx::query(AGENT_ASSET_RECIPE_SNAPSHOT_QUERY)
            .bind(&expected.asset_id)
            .bind(&expected.source_path)
            .fetch_all(&mut **transaction)
            .await?;
        if rows.len() != 1 {
            return Err(agent_state_error(
                "agent recipe source or catalog snapshot changed; owned write was not applied",
            ));
        }
        let actual = agent_recipe_snapshot_from_row(
            rows.into_iter().next().expect("one asset recipe row"),
            &expected.asset_id,
            &expected.source_path,
        )?;
        if !same_agent_asset_recipe_snapshot(&actual, expected) {
            return Err(agent_state_error(
                "agent recipe source or catalog snapshot changed; owned write was not applied",
            ));
        }
        Ok(())
    }

    pub async fn agent_catalog_row_snapshot(
        &self,
        source_path: &str,
    ) -> Result<AgentCatalogRowSnapshot, sqlx::Error> {
        if source_path.trim().is_empty() {
            return Err(agent_state_error(
                "catalog row snapshot requires a source path",
            ));
        }
        let recipe = self.agent_asset_recipe_snapshot("", source_path).await?;
        Ok(AgentCatalogRowSnapshot {
            schema: "agent-catalog-row:v1".into(),
            state: recipe.state,
            asset_id: recipe.asset_id,
            source_path: recipe.source_path,
            source_sha256: recipe.source_sha256,
            catalog_row_state: recipe.catalog_row_state,
        })
    }

    pub async fn restore_agent_asset_recipe(
        &self,
        expected: &AgentAssetRecipeSnapshot,
        before: &AgentAssetRecipeSnapshot,
    ) -> Result<(), sqlx::Error> {
        if expected.schema != "agent-asset-recipe:v1"
            || before.schema != "agent-asset-recipe:v1"
            || expected.state != "present"
            || before.state != "present"
            || expected.asset_id != before.asset_id
            || expected.source_path != before.source_path
            || expected.source_sha256 != before.source_sha256
            || expected.catalog_row_state != before.catalog_row_state
        {
            return Err(agent_state_error(
                "agent recipe baseline does not match the expected asset",
            ));
        }

        let mut transaction = self.pool.begin().await?;
        let locked = sqlx::query("SELECT asset_id FROM assets WHERE asset_id = $1 FOR UPDATE")
            .bind(&expected.asset_id)
            .fetch_optional(&mut *transaction)
            .await?;
        if locked.is_none() {
            return Err(agent_state_error("agent recipe asset row is missing"));
        }
        let _ = sqlx::query("SELECT recipe_json FROM edit_recipes WHERE asset_id = $1 FOR UPDATE")
            .bind(&expected.asset_id)
            .fetch_optional(&mut *transaction)
            .await?;
        let _ = sqlx::query(
            "SELECT recipe_payload FROM canonical_edit_recipes_v13 WHERE asset_id = $1 FOR UPDATE",
        )
        .bind(&expected.asset_id)
        .fetch_optional(&mut *transaction)
        .await?;
        let rows = sqlx::query(AGENT_ASSET_RECIPE_SNAPSHOT_QUERY)
            .bind(&expected.asset_id)
            .bind(&expected.source_path)
            .fetch_all(&mut *transaction)
            .await?;
        if rows.len() != 1
            || agent_recipe_snapshot_from_row(
                rows.into_iter().next().expect("one row"),
                &expected.asset_id,
                &expected.source_path,
            )? != *expected
        {
            return Err(agent_state_error(
                "agent recipe checkpoint changed; catalog data was preserved",
            ));
        }

        match &before.legacy_recipe {
            Some(recipe) => {
                sqlx::query(
                    r#"INSERT INTO edit_recipes (asset_id, recipe_json, updated_at)
                       VALUES ($1, $2, NOW())
                       ON CONFLICT (asset_id) DO UPDATE SET recipe_json = EXCLUDED.recipe_json, updated_at = NOW()"#,
                )
                .bind(&before.asset_id)
                .bind(recipe)
                .execute(&mut *transaction)
                .await?;
            }
            None => {
                sqlx::query("DELETE FROM edit_recipes WHERE asset_id = $1")
                    .bind(&before.asset_id)
                    .execute(&mut *transaction)
                    .await?;
            }
        }
        match &before.canonical_recipe {
            Some(recipe) => {
                sqlx::query(
                    r#"INSERT INTO canonical_edit_recipes_v13
                           (asset_id, recipe_payload, cache_key, provenance_payload, updated_at)
                       VALUES ($1, $2, $3, $4::jsonb, NOW())
                       ON CONFLICT (asset_id) DO UPDATE SET
                           recipe_payload = EXCLUDED.recipe_payload,
                           cache_key = EXCLUDED.cache_key,
                           provenance_payload = EXCLUDED.provenance_payload,
                           updated_at = NOW()"#,
                )
                .bind(&before.asset_id)
                .bind(&recipe.recipe_payload)
                .bind(&recipe.cache_key)
                .bind(&recipe.provenance_payload)
                .execute(&mut *transaction)
                .await?;
            }
            None => {
                sqlx::query("DELETE FROM canonical_edit_recipes_v13 WHERE asset_id = $1")
                    .bind(&before.asset_id)
                    .execute(&mut *transaction)
                    .await?;
            }
        }
        transaction.commit().await?;
        Ok(())
    }

    pub async fn remove_agent_catalog_row_if_unchanged(
        &self,
        expected: &AgentCatalogRowSnapshot,
    ) -> Result<(), sqlx::Error> {
        if expected.schema != "agent-catalog-row:v1"
            || expected.state != "present"
            || expected.asset_id.trim().is_empty()
            || expected.source_path.trim().is_empty()
            || expected.source_sha256.trim().is_empty()
        {
            return Err(agent_state_error(
                "invalid expected agent catalog row receipt",
            ));
        }
        let mut transaction = self.pool.begin().await?;
        let locked = sqlx::query("SELECT asset_id FROM assets WHERE asset_id = $1 FOR UPDATE")
            .bind(&expected.asset_id)
            .fetch_optional(&mut *transaction)
            .await?;
        if locked.is_none() {
            return Err(agent_state_error("agent catalog row is already absent"));
        }
        // Row locks prevent a late recipe insert through the asset foreign key;
        // explicit row locks also serialize all writes already in progress.
        let _ = sqlx::query("SELECT recipe_json FROM edit_recipes WHERE asset_id = $1 FOR UPDATE")
            .bind(&expected.asset_id)
            .fetch_optional(&mut *transaction)
            .await?;
        let _ = sqlx::query(
            "SELECT recipe_payload FROM canonical_edit_recipes_v13 WHERE asset_id = $1 FOR UPDATE",
        )
        .bind(&expected.asset_id)
        .fetch_optional(&mut *transaction)
        .await?;
        let rows = sqlx::query(AGENT_ASSET_RECIPE_SNAPSHOT_QUERY)
            .bind(&expected.asset_id)
            .bind(&expected.source_path)
            .fetch_all(&mut *transaction)
            .await?;
        if rows.len() != 1 {
            return Err(agent_state_error(
                "agent catalog row identity changed; catalog data was preserved",
            ));
        }
        let current = agent_recipe_snapshot_from_row(
            rows.into_iter().next().expect("one row"),
            &expected.asset_id,
            &expected.source_path,
        )?;
        let current_row = AgentCatalogRowSnapshot {
            schema: "agent-catalog-row:v1".into(),
            state: current.state,
            asset_id: current.asset_id,
            source_path: current.source_path,
            source_sha256: current.source_sha256,
            catalog_row_state: current.catalog_row_state,
        };
        if current_row != *expected {
            return Err(agent_state_error(
                "agent catalog row changed; catalog data was preserved",
            ));
        }
        if current.legacy_recipe.is_some() || current.canonical_recipe.is_some() {
            return Err(agent_state_error(
                "agent recipe owner must restore its checkpoint before removing the catalog row",
            ));
        }
        let has_references: bool = sqlx::query_scalar(
            r#"SELECT EXISTS(
                   SELECT 1 FROM project_assets WHERE asset_id = $1
                   UNION ALL SELECT 1 FROM asset_review_states WHERE asset_id = $1
                   UNION ALL SELECT 1 FROM asset_relinks WHERE asset_id = $1
                   UNION ALL SELECT 1 FROM timeline_documents WHERE asset_id = $1
               )"#,
        )
        .bind(&expected.asset_id)
        .fetch_one(&mut *transaction)
        .await?;
        if has_references {
            return Err(agent_state_error(
                "agent catalog row has user-owned references; it was preserved",
            ));
        }
        let deleted = sqlx::query("DELETE FROM assets WHERE asset_id = $1")
            .bind(&expected.asset_id)
            .execute(&mut *transaction)
            .await?;
        if deleted.rows_affected() != 1 {
            return Err(agent_state_error(
                "agent catalog row changed during removal; catalog data was preserved",
            ));
        }
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

    pub async fn upsert_asset_review_states(
        &self,
        states: &[AssetReviewStateRecord],
    ) -> Result<ReviewStateBatchResult, sqlx::Error> {
        validate_review_state_batch(states).map_err(|error| sqlx::Error::Protocol(error.into()))?;
        let payload = serde_json::to_string(states)
            .map_err(|error| sqlx::Error::Protocol(error.to_string()))?;
        let mut transaction = self.pool.begin().await?;
        // Joining the asset table lets deleted/local-only assets be reported
        // individually. Any statement or commit failure rolls back the batch.
        let applied = sqlx::query_scalar::<_, String>(
            r#"INSERT INTO asset_review_states (asset_id, rating, picked, rejected, color_label, updated_at)
            SELECT requested.asset_id, requested.rating, requested.picked, requested.rejected,
                   requested.color_label, NOW()
            FROM jsonb_to_recordset($1::text::jsonb) AS requested(
                asset_id text, rating integer, picked boolean, rejected boolean, color_label text)
            JOIN assets ON assets.asset_id = requested.asset_id
            ORDER BY requested.asset_id
            ON CONFLICT (asset_id) DO UPDATE SET rating = EXCLUDED.rating,
                picked = EXCLUDED.picked, rejected = EXCLUDED.rejected,
                color_label = EXCLUDED.color_label, updated_at = NOW()
            RETURNING asset_id"#,
        ).bind(payload).fetch_all(&mut *transaction).await?;
        transaction.commit().await?;
        let saved: std::collections::HashSet<_> = applied.iter().collect();
        let missing = states
            .iter()
            .filter(|state| !saved.contains(&state.asset_id))
            .map(|state| state.asset_id.clone())
            .collect();
        Ok(ReviewStateBatchResult { applied, missing })
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
                document_json = COALESCE($8, timeline_documents.document_json),
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

        // Metadata-only replacement must retain each existing durable backup.
        // Omitted records are removed as before; incoming Some(JSON), including
        // an explicit empty object, still replaces that record's document.
        let timeline_ids: Vec<&str> = documents
            .iter()
            .map(|document| document.timeline_id.as_str())
            .collect();
        sqlx::query(
            r#"
            DELETE FROM timeline_documents WHERE NOT (timeline_id = ANY($1))
            "#,
        )
        .bind(&timeline_ids)
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
                ON CONFLICT (timeline_id) DO UPDATE SET
                    asset_id = EXCLUDED.asset_id,
                    name = EXCLUDED.name,
                    document_path = EXCLUDED.document_path,
                    render_path = EXCLUDED.render_path,
                    poster_path = EXCLUDED.poster_path,
                    duration_seconds = EXCLUDED.duration_seconds,
                    document_json = COALESCE($8, timeline_documents.document_json),
                    created_at = COALESCE($9::timestamptz, timeline_documents.created_at),
                    updated_at = EXCLUDED.updated_at
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

            async fn update_project_memberships(
                &self,
                project_name: &str,
                added: &[String],
                removed: &[String],
            ) -> Result<Vec<String>, sqlx::Error> {
                <$repository>::update_project_memberships(self, project_name, added, removed).await
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

            async fn upsert_asset_review_states(
                &self,
                states: &[AssetReviewStateRecord],
            ) -> Result<ReviewStateBatchResult, sqlx::Error> {
                <$repository>::upsert_asset_review_states(self, states).await
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

    async fn update_project_memberships(
        &self,
        project_name: &str,
        added: &[String],
        removed: &[String],
    ) -> Result<Vec<String>, sqlx::Error> {
        dispatch_catalog_runtime!(
            self,
            update_project_memberships,
            project_name,
            added,
            removed
        )
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

    async fn upsert_asset_review_states(
        &self,
        states: &[AssetReviewStateRecord],
    ) -> Result<ReviewStateBatchResult, sqlx::Error> {
        dispatch_catalog_runtime!(self, upsert_asset_review_states, states)
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
        capture_timestamp: row.try_get("capture_timestamp")?,
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

fn agent_state_error(message: &str) -> sqlx::Error {
    sqlx::Error::Protocol(message.to_string())
}

fn validate_agent_task_recipe_snapshot(
    snapshot: &AgentAssetRecipeSnapshot,
    expected_asset_id: &str,
) -> Result<(), sqlx::Error> {
    if snapshot.schema != "agent-asset-recipe:v1"
        || snapshot.state != "present"
        || snapshot.asset_id.trim().is_empty()
        || snapshot.asset_id != expected_asset_id
        || !Path::new(&snapshot.source_path).is_absolute()
        || snapshot.source_sha256.len() != 64
        || !snapshot
            .source_sha256
            .bytes()
            .all(|byte| byte.is_ascii_hexdigit())
    {
        return Err(agent_state_error(
            "owned recipe write requires an exact present source snapshot",
        ));
    }
    Ok(())
}

fn same_agent_asset_recipe_snapshot(
    actual: &AgentAssetRecipeSnapshot,
    expected: &AgentAssetRecipeSnapshot,
) -> bool {
    actual.schema == expected.schema
        && actual.state == expected.state
        && actual.asset_id == expected.asset_id
        && actual.source_path == expected.source_path
        && actual.source_sha256 == expected.source_sha256
        && same_optional_json_value(&actual.legacy_recipe, &expected.legacy_recipe)
        && actual.canonical_recipe == expected.canonical_recipe
        && same_json_value(&actual.catalog_row_state, &expected.catalog_row_state)
}

fn same_optional_json_value(actual: &Option<Value>, expected: &Option<Value>) -> bool {
    match (actual, expected) {
        (Some(actual), Some(expected)) => same_json_value(actual, expected),
        (None, None) => true,
        _ => false,
    }
}

/// Compare JSON numbers by their exact decimal value. Qt's JSON roundtrip can
/// serialize `0.0` as `0`; converting both sides through f64 would also make
/// distinct integers above 2^53 compare equal, so normalize the decimal text
/// instead of coercing integers to floating point.
fn same_json_value(actual: &Value, expected: &Value) -> bool {
    match (actual, expected) {
        (Value::Null, Value::Null) => true,
        (Value::Bool(actual), Value::Bool(expected)) => actual == expected,
        (Value::Number(actual), Value::Number(expected)) => {
            normalized_json_number(actual) == normalized_json_number(expected)
        }
        (Value::String(actual), Value::String(expected)) => actual == expected,
        (Value::Array(actual), Value::Array(expected)) => {
            actual.len() == expected.len()
                && actual
                    .iter()
                    .zip(expected)
                    .all(|(actual, expected)| same_json_value(actual, expected))
        }
        (Value::Object(actual), Value::Object(expected)) => {
            actual.len() == expected.len()
                && actual.iter().all(|(key, actual)| {
                    expected
                        .get(key)
                        .is_some_and(|expected| same_json_value(actual, expected))
                })
        }
        _ => false,
    }
}

fn normalized_json_number(number: &serde_json::Number) -> Option<(bool, String, i64)> {
    let text = number.to_string();
    let (mantissa, exponent) = match text.find(|character| character == 'e' || character == 'E') {
        Some(index) => {
            let (mantissa, exponent) = text.split_at(index);
            (mantissa, exponent[1..].parse::<i64>().ok()?)
        }
        None => (text.as_str(), 0),
    };
    let (negative, unsigned_mantissa) = match mantissa.strip_prefix('-') {
        Some(unsigned) => (true, unsigned),
        None => (false, mantissa),
    };
    let (integer, fractional) = match unsigned_mantissa.split_once('.') {
        Some((integer, fractional)) => (integer, fractional),
        None => (unsigned_mantissa, ""),
    };
    let fractional_digits = i64::try_from(fractional.len()).ok()?;
    let mut digits = String::with_capacity(integer.len() + fractional.len());
    digits.push_str(integer);
    digits.push_str(fractional);
    let first_nonzero = digits.bytes().position(|byte| byte != b'0');
    let Some(first_nonzero) = first_nonzero else {
        return Some((false, "0".into(), 0));
    };
    digits.drain(..first_nonzero);
    let mut power = exponent.checked_sub(fractional_digits)?;
    while digits.ends_with('0') {
        digits.pop();
        power = power.checked_add(1)?;
    }
    Some((negative, digits, power))
}

fn sha256_source_file(path: &Path) -> Result<String, sqlx::Error> {
    let mut file = fs::File::open(path).map_err(sqlx::Error::Io)?;
    let mut digest = Sha256::new();
    let mut buffer = [0_u8; 1024 * 1024];
    loop {
        let read = file.read(&mut buffer).map_err(sqlx::Error::Io)?;
        if read == 0 {
            break;
        }
        digest.update(&buffer[..read]);
    }
    Ok(format!("{:x}", digest.finalize()))
}

fn agent_recipe_snapshot_from_row(
    row: PgRow,
    requested_asset_id: &str,
    requested_source_path: &str,
) -> Result<AgentAssetRecipeSnapshot, sqlx::Error> {
    let asset_id: String = row.try_get("asset_id")?;
    if !requested_asset_id.is_empty() && asset_id != requested_asset_id {
        return Err(agent_state_error(
            "agent asset ID no longer identifies the requested source",
        ));
    }
    let root_path: String = row.try_get("root_path")?;
    let relative_path: String = row.try_get("relative_path")?;
    let stored_path = Path::new(&root_path).join(&relative_path);
    let canonical_path = fs::canonicalize(&stored_path).map_err(sqlx::Error::Io)?;
    if !canonical_path.is_file() {
        return Err(agent_state_error(
            "agent asset source is not a regular file",
        ));
    }
    if !requested_source_path.is_empty() {
        let requested_path = fs::canonicalize(requested_source_path).map_err(sqlx::Error::Io)?;
        if requested_path != canonical_path {
            return Err(agent_state_error("agent asset source path changed"));
        }
    }
    let source_path = canonical_path.to_string_lossy().into_owned();
    let source_sha256 = sha256_source_file(&canonical_path)?;
    let legacy_recipe: Option<Value> = row.try_get("recipe_json")?;
    let canonical_payload: Option<String> = row.try_get("canonical_recipe_payload")?;
    let canonical_recipe = match canonical_payload {
        Some(recipe_payload) => Some(AgentCanonicalRecipe {
            recipe_payload,
            cache_key: row.try_get("canonical_cache_key")?,
            provenance_payload: row.try_get("canonical_provenance_payload")?,
        }),
        None => None,
    };
    let catalog_row_state: Value = row.try_get("catalog_row_state")?;
    Ok(AgentAssetRecipeSnapshot {
        schema: "agent-asset-recipe:v1".into(),
        state: "present".into(),
        asset_id,
        source_path,
        source_sha256,
        legacy_recipe,
        canonical_recipe,
        catalog_row_state,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn older_asset_payloads_default_capture_timestamp_to_none() {
        let legacy = serde_json::json!({
            "asset_id": "legacy-asset",
            "capture_year": 2020,
            "capture_month": 2,
            "capture_day": 29,
            "camera_make": "Camera",
            "camera_model": "Body",
            "lens_model": "Lens",
            "iso": null,
            "shutter_speed": "",
            "aperture": null,
            "import_mode": "Referenced",
            "location": {
                "drive_id": "drive",
                "root_path": "/photos",
                "relative_path": "legacy.jpg",
                "online": true
            }
        });
        let record: CatalogAssetRecord =
            serde_json::from_value(legacy).expect("legacy asset payload");
        assert_eq!(record.capture_timestamp, None);
        let serialized = serde_json::to_value(record).expect("serialize asset with no EXIF time");
        assert_eq!(serialized.get("capture_timestamp"), Some(&Value::Null));
    }

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
    fn review_state_batch_validates_bounds_and_fields() {
        let state = AssetReviewStateRecord {
            asset_id: "asset".into(),
            rating: 3,
            picked: true,
            rejected: false,
            color_label: "blue".into(),
        };
        assert!(validate_review_state_batch(std::slice::from_ref(&state)).is_ok());
        assert!(validate_review_state_batch(&[]).is_err());
        assert!(validate_review_state_batch(&[state.clone(), state.clone()]).is_err());
        for invalid in [
            AssetReviewStateRecord {
                rating: 6,
                ..state.clone()
            },
            AssetReviewStateRecord {
                rating: -1,
                ..state.clone()
            },
            AssetReviewStateRecord {
                rejected: true,
                ..state.clone()
            },
            AssetReviewStateRecord {
                color_label: "invalid".into(),
                ..state.clone()
            },
            AssetReviewStateRecord {
                asset_id: " ".into(),
                ..state.clone()
            },
            AssetReviewStateRecord {
                asset_id: " asset".into(),
                ..state.clone()
            },
        ] {
            assert!(validate_review_state_batch(&[invalid]).is_err());
        }
        let states: Vec<_> = (0..256)
            .map(|i| AssetReviewStateRecord {
                asset_id: i.to_string(),
                ..state.clone()
            })
            .collect();
        assert!(validate_review_state_batch(&states).is_ok());
        let mut too_many = states;
        too_many.push(AssetReviewStateRecord {
            asset_id: "last".into(),
            ..state
        });
        assert!(validate_review_state_batch(&too_many).is_err());
    }

    #[tokio::test]
    #[ignore = "requires an explicitly isolated PHOTO_EDITOR_CULLING_TEST_DATABASE_URL"]
    async fn review_state_batch_postgres_commit_missing_and_rollback() {
        let url = std::env::var("PHOTO_EDITOR_CULLING_TEST_DATABASE_URL")
            .expect("isolated database required");
        let pool = sqlx::postgres::PgPoolOptions::new()
            .max_connections(1)
            .connect(&url)
            .await
            .unwrap();
        // This test creates only its own schema. Never touch production tables.
        let schema = format!("culling_test_{}", std::process::id());
        sqlx::query(&format!("CREATE SCHEMA {schema}"))
            .execute(&pool)
            .await
            .unwrap();
        sqlx::query(&format!("SET search_path TO {schema}"))
            .execute(&pool)
            .await
            .unwrap();
        sqlx::raw_sql(
            "CREATE TABLE assets (asset_id text PRIMARY KEY); CREATE TABLE asset_review_states (
            asset_id text PRIMARY KEY REFERENCES assets(asset_id) ON DELETE CASCADE,
            rating integer NOT NULL CHECK (rating BETWEEN 0 AND 5), picked boolean NOT NULL,
            rejected boolean NOT NULL, color_label text NOT NULL, updated_at timestamptz NOT NULL,
            CHECK (NOT (picked AND rejected))); INSERT INTO assets VALUES ('a'), ('b');",
        )
        .execute(&pool)
        .await
        .unwrap();
        let catalog = CatalogService { pool: pool.clone() };
        let state = |id: &str, rating| AssetReviewStateRecord {
            asset_id: id.into(),
            rating,
            picked: false,
            rejected: false,
            color_label: "purple".into(),
        };
        let result = catalog
            .upsert_asset_review_states(&[state("a", 4), state("missing", 2), state("b", 1)])
            .await
            .unwrap();
        assert_eq!(result.applied, vec!["a", "b"]);
        assert_eq!(result.missing, vec!["missing"]);
        assert_eq!(catalog.list_asset_review_states().await.unwrap().len(), 2);
        // Force the second row to fail after the first row has executed.
        sqlx::raw_sql("CREATE FUNCTION reject_test_rating() RETURNS trigger LANGUAGE plpgsql AS $$
            BEGIN IF NEW.rating = 3 THEN RAISE EXCEPTION 'fixture failure'; END IF; RETURN NEW; END $$;
            CREATE TRIGGER reject_rating BEFORE INSERT OR UPDATE ON asset_review_states
            FOR EACH ROW EXECUTE FUNCTION reject_test_rating();").execute(&pool).await.unwrap();
        assert!(catalog
            .upsert_asset_review_states(&[state("a", 5), state("b", 3)])
            .await
            .is_err());
        let actual = catalog.list_asset_review_states().await.unwrap();
        assert_eq!(actual, vec![state("a", 4), state("b", 1)]);
        sqlx::query("DROP TRIGGER reject_rating ON asset_review_states")
            .execute(&pool)
            .await
            .unwrap();
        let cleared = AssetReviewStateRecord {
            asset_id: "a".into(),
            rating: 0,
            picked: false,
            rejected: false,
            color_label: String::new(),
        };
        catalog
            .upsert_asset_review_states(std::slice::from_ref(&cleared))
            .await
            .unwrap();
        assert_eq!(
            catalog.list_asset_review_states().await.unwrap()[0],
            cleared
        );
        sqlx::query("DELETE FROM assets WHERE asset_id = 'a'")
            .execute(&pool)
            .await
            .unwrap();
        let result = catalog
            .upsert_asset_review_states(&[state("a", 5), state("b", 2)])
            .await
            .unwrap();
        assert_eq!(result.applied, vec!["b"]);
        assert_eq!(result.missing, vec!["a"]);
        sqlx::query(&format!("DROP SCHEMA {schema} CASCADE"))
            .execute(&pool)
            .await
            .unwrap();
    }

    #[tokio::test]
    #[ignore = "requires an explicitly isolated PHOTO_EDITOR_AGENT_TASK_TEST_DATABASE_URL"]
    async fn agent_recipe_owner_restores_before_catalog_row_and_preserves_manual_conflicts() {
        let url = std::env::var("PHOTO_EDITOR_AGENT_TASK_TEST_DATABASE_URL")
            .expect("isolated database required");
        let pool = sqlx::postgres::PgPoolOptions::new()
            .max_connections(1)
            .connect(&url)
            .await
            .unwrap();
        let schema = format!("agent_task_test_{}", std::process::id());
        sqlx::query(&format!("CREATE SCHEMA {schema}"))
            .execute(&pool)
            .await
            .unwrap();
        sqlx::query(&format!("SET search_path TO {schema}"))
            .execute(&pool)
            .await
            .unwrap();
        sqlx::raw_sql(
            r#"CREATE TABLE drives (drive_id text PRIMARY KEY, is_online boolean NOT NULL DEFAULT true);
               CREATE TABLE assets (
                   asset_id text PRIMARY KEY, drive_id text NOT NULL REFERENCES drives(drive_id),
                   root_path text NOT NULL, relative_path text NOT NULL, import_mode text NOT NULL,
                   rating integer NOT NULL DEFAULT 0, flag_state text NOT NULL DEFAULT 'unflagged',
                   color_label text NOT NULL DEFAULT '', updated_at timestamptz NOT NULL DEFAULT NOW());
               CREATE TABLE edit_recipes (
                   asset_id text PRIMARY KEY REFERENCES assets(asset_id), recipe_json jsonb NOT NULL,
                   updated_at timestamptz NOT NULL DEFAULT NOW());
               CREATE TABLE canonical_edit_recipes_v13 (
                   asset_id text PRIMARY KEY REFERENCES assets(asset_id), recipe_payload text NOT NULL,
                   cache_key text NOT NULL, provenance_payload jsonb NOT NULL,
                   updated_at timestamptz NOT NULL DEFAULT NOW());
               CREATE TABLE projects (name text PRIMARY KEY);
               CREATE TABLE project_assets (
                   project_name text NOT NULL REFERENCES projects(name) ON DELETE CASCADE,
                   asset_id text NOT NULL REFERENCES assets(asset_id) ON DELETE CASCADE,
                   created_at timestamptz NOT NULL DEFAULT NOW(), PRIMARY KEY (project_name, asset_id));
               CREATE TABLE asset_review_states (
                   asset_id text PRIMARY KEY REFERENCES assets(asset_id) ON DELETE CASCADE,
                   rating integer NOT NULL DEFAULT 0, picked boolean NOT NULL DEFAULT false,
                   rejected boolean NOT NULL DEFAULT false, color_label text NOT NULL DEFAULT '',
                   updated_at timestamptz NOT NULL DEFAULT NOW());
               CREATE TABLE asset_relinks (
                   asset_id text PRIMARY KEY REFERENCES assets(asset_id) ON DELETE CASCADE,
                   relinked_file_path text NOT NULL, updated_at timestamptz NOT NULL DEFAULT NOW());
               CREATE TABLE timeline_documents (
                   timeline_id text PRIMARY KEY, asset_id text NOT NULL REFERENCES assets(asset_id) ON DELETE CASCADE,
                   name text NOT NULL, document_path text NOT NULL, render_path text NOT NULL,
                   poster_path text NOT NULL, document_json jsonb NOT NULL DEFAULT '{}'::jsonb);
               CREATE TABLE recipe_migration_records (
                   asset_id text PRIMARY KEY REFERENCES assets(asset_id) ON DELETE CASCADE,
                   source_recipe_version integer NOT NULL, source_renderer text NOT NULL,
                   target_renderer text NOT NULL, profile_identities jsonb NOT NULL DEFAULT '[]'::jsonb,
                   plan_hash text NOT NULL, evidence_hash text NOT NULL, approval_time timestamptz,
                   rollback_pointer text NOT NULL, status text NOT NULL,
                   created_at timestamptz NOT NULL DEFAULT NOW(), updated_at timestamptz NOT NULL DEFAULT NOW());"#,
        )
        .execute(&pool)
        .await
        .unwrap();

        let directory = tempfile::tempdir().unwrap();
        let source = directory.path().join("canvas.png");
        fs::write(&source, b"owned canvas source").unwrap();
        sqlx::query("INSERT INTO drives (drive_id) VALUES ('canvas-drive')")
            .execute(&pool)
            .await
            .unwrap();
        sqlx::query(
            "INSERT INTO assets (asset_id, drive_id, root_path, relative_path, import_mode) VALUES ('canvas', 'canvas-drive', $1, 'canvas.png', 'referenced')",
        )
        .bind(directory.path().to_string_lossy().as_ref())
        .execute(&pool)
        .await
        .unwrap();
        let catalog = CatalogService { pool: pool.clone() };

        let recipe_before = catalog
            .agent_asset_recipe_snapshot("canvas", source.to_str().unwrap())
            .await
            .unwrap();
        let row_before = catalog
            .agent_catalog_row_snapshot(source.to_str().unwrap())
            .await
            .unwrap();
        let legacy = serde_json::json!({"adjustments": {"exposure": 0.3}});
        let canvas_canonical = AgentCanonicalRecipe {
            recipe_payload: "{\"schema\":\"recipe:v1\"}".into(),
            cache_key: "cache-one".into(),
            provenance_payload:
                "{\"schema\":\"photo-editor-edit-recipe-provenance:v1\",\"presetApplications\":[]}"
                    .into(),
        };
        catalog
            .save_agent_edit_recipe_if_unchanged(
                "canvas",
                &recipe_before,
                &legacy,
                Some(&canvas_canonical),
            )
            .await
            .unwrap();
        let recipe_after = catalog
            .agent_asset_recipe_snapshot("canvas", source.to_str().unwrap())
            .await
            .unwrap();
        let row_after = catalog
            .agent_catalog_row_snapshot(source.to_str().unwrap())
            .await
            .unwrap();
        assert_ne!(
            recipe_after, recipe_before,
            "the owned design save must advance the recipe receipt"
        );
        assert_eq!(
            row_before, row_after,
            "recipe changes belong to the separate recipe resource"
        );

        // Native photo restoration may persist its blank canvas recipe before
        // the catalog row resource is removed. Revert must drain that producer,
        // then CAS the exact blank recipe back to the original absent recipe.
        catalog
            .upsert_edit_recipe_compatibility_pair(
                "canvas",
                &serde_json::json!({"adjustments": {}}),
                "{}",
                "cache-blank",
                "{\"schema\":\"photo-editor-edit-recipe-provenance:v1\",\"presetApplications\":[]}",
            )
            .await
            .unwrap();
        let native_restore_checkpoint = catalog
            .agent_asset_recipe_snapshot("canvas", source.to_str().unwrap())
            .await
            .unwrap();
        assert_eq!(
            catalog
                .agent_catalog_row_snapshot(source.to_str().unwrap())
                .await
                .unwrap(),
            row_after
        );
        catalog
            .restore_agent_asset_recipe(&native_restore_checkpoint, &recipe_before)
            .await
            .unwrap();
        catalog
            .remove_agent_catalog_row_if_unchanged(&row_after)
            .await
            .unwrap();
        assert_eq!(
            catalog
                .agent_catalog_row_snapshot(source.to_str().unwrap())
                .await
                .unwrap()
                .state,
            "absent"
        );

        // The desktop's QJsonDocument roundtrip may turn a valid JSON `0.0`
        // into `0`. That representation change is semantically identical,
        // while a distinct integer above 2^53 must remain a conflict.
        let numeric_source = directory.path().join("numeric.png");
        fs::write(&numeric_source, b"numeric recipe source").unwrap();
        sqlx::query(
            "INSERT INTO assets (asset_id, drive_id, root_path, relative_path, import_mode) VALUES ('numeric', 'canvas-drive', $1, 'numeric.png', 'referenced')",
        )
        .bind(directory.path().to_string_lossy().as_ref())
        .execute(&pool)
        .await
        .unwrap();
        sqlx::query("INSERT INTO edit_recipes (asset_id, recipe_json) VALUES ('numeric', $1)")
            .bind(serde_json::json!({"exposure": 0.0}))
            .execute(&pool)
            .await
            .unwrap();
        let numeric_baseline = catalog
            .agent_asset_recipe_snapshot("numeric", numeric_source.to_str().unwrap())
            .await
            .unwrap();
        let mut qt_roundtrip_baseline = numeric_baseline.clone();
        qt_roundtrip_baseline.legacy_recipe = Some(serde_json::json!({"exposure": 0}));
        catalog
            .save_agent_edit_recipe_if_unchanged(
                "numeric",
                &qt_roundtrip_baseline,
                &serde_json::json!({"exposure": 0.5}),
                None,
            )
            .await
            .expect("0.0 and its QJson roundtrip representation 0 have equal numeric value");
        let numeric_checkpoint = catalog
            .agent_asset_recipe_snapshot("numeric", numeric_source.to_str().unwrap())
            .await
            .unwrap();
        sqlx::query("UPDATE edit_recipes SET recipe_json = $1 WHERE asset_id = 'numeric'")
            .bind(serde_json::json!({"integer": 9_007_199_254_740_993_u64}))
            .execute(&pool)
            .await
            .unwrap();
        let mut rounded_large_integer = numeric_checkpoint;
        rounded_large_integer.legacy_recipe =
            Some(serde_json::json!({"integer": 9_007_199_254_740_992_f64}));
        let distinct_large_integer = catalog
            .save_agent_edit_recipe_if_unchanged(
                "numeric",
                &rounded_large_integer,
                &serde_json::json!({"integer": 1}),
                None,
            )
            .await
            .unwrap_err();
        assert!(distinct_large_integer
            .to_string()
            .contains("snapshot changed"));
        assert_eq!(
            catalog
                .agent_asset_recipe_snapshot("numeric", numeric_source.to_str().unwrap())
                .await
                .unwrap()
                .legacy_recipe
                .unwrap(),
            serde_json::json!({"integer": 9_007_199_254_740_993_u64}),
            "a distinct large integer must be preserved after rejection"
        );

        let second_source = directory.path().join("manual.png");
        fs::write(&second_source, b"manual photo").unwrap();
        sqlx::query(
            "INSERT INTO assets (asset_id, drive_id, root_path, relative_path, import_mode) VALUES ('manual', 'canvas-drive', $1, 'manual.png', 'referenced')",
        )
        .bind(directory.path().to_string_lossy().as_ref())
        .execute(&pool)
        .await
        .unwrap();
        let recipe_baseline = catalog
            .agent_asset_recipe_snapshot("manual", second_source.to_str().unwrap())
            .await
            .unwrap();
        let task_recipe = serde_json::json!({"adjustments": {"exposure": 0.3}});
        let task_canonical = AgentCanonicalRecipe {
            recipe_payload: "{\"schema\":\"recipe:v1\",\"exposure\":0.3}".into(),
            cache_key: "task-cache".into(),
            provenance_payload:
                "{\"schema\":\"photo-editor-edit-recipe-provenance:v1\",\"presetApplications\":[]}"
                    .into(),
        };
        catalog
            .save_agent_edit_recipe_if_unchanged(
                "manual",
                &recipe_baseline,
                &task_recipe,
                Some(&task_canonical),
            )
            .await
            .unwrap();
        let owned_checkpoint = catalog
            .agent_asset_recipe_snapshot("manual", second_source.to_str().unwrap())
            .await
            .unwrap();
        let manual_recipe = serde_json::json!({"adjustments": {"exposure": 1.1}});
        catalog
            .upsert_edit_recipe_compatibility_pair(
                "manual",
                &manual_recipe,
                "{\"schema\":\"recipe:v1\",\"exposure\":1.1}",
                "manual-cache",
                "{\"schema\":\"photo-editor-edit-recipe-provenance:v1\",\"presetApplications\":[]}",
            )
            .await
            .unwrap();
        let stale_owned = catalog
            .save_agent_edit_recipe_if_unchanged(
                "manual",
                &owned_checkpoint,
                &task_recipe,
                Some(&task_canonical),
            )
            .await
            .unwrap_err();
        assert!(stale_owned.to_string().contains("snapshot changed"));
        let error = catalog
            .restore_agent_asset_recipe(&owned_checkpoint, &recipe_baseline)
            .await
            .unwrap_err();
        assert!(error.to_string().contains("checkpoint changed"));
        let current = catalog
            .agent_asset_recipe_snapshot("manual", second_source.to_str().unwrap())
            .await
            .unwrap();
        assert_eq!(
            current.legacy_recipe.unwrap()["adjustments"]["exposure"],
            1.1
        );
        assert_eq!(current.canonical_recipe.unwrap().cache_key, "manual-cache");

        let migration_source = directory.path().join("migration.png");
        fs::write(&migration_source, b"migration photo").unwrap();
        sqlx::query(
            "INSERT INTO assets (asset_id, drive_id, root_path, relative_path, import_mode) VALUES ('migration', 'canvas-drive', $1, 'migration.png', 'referenced')",
        )
        .bind(directory.path().to_string_lossy().as_ref())
        .execute(&pool)
        .await
        .unwrap();
        let migration_baseline = catalog
            .agent_asset_recipe_snapshot("migration", migration_source.to_str().unwrap())
            .await
            .unwrap();
        let migration_legacy = serde_json::json!({"recipeVersion": 12, "colorPipeline": {
            "photographicRenderingModel": "legacy-oklch-v1"}});
        let migration_record = RecipeMigrationRecord {
            asset_id: "migration".into(),
            source_recipe_version: 12,
            source_renderer: "legacy-oklch-v1".into(),
            target_renderer: "aces2-ocio-v1".into(),
            profile_identities: vec![],
            plan_hash: "migration-plan".into(),
            evidence_hash: "migration-evidence".into(),
            approval_time: Some("2026-10-06T00:00:00Z".into()),
            rollback_pointer: "isolated-test-backup".into(),
            status: "accepted".into(),
            created_at: String::new(),
            updated_at: String::new(),
        };
        let migration_provenance =
            "{\"schema\":\"photo-editor-edit-recipe-provenance:v1\",\"presetApplications\":[]}";
        catalog
            .accept_agent_recipe_migration_if_unchanged(
                &migration_baseline,
                &migration_legacy,
                "{\"schema\":\"recipe:v1\",\"migration\":true}",
                "migration-cache",
                migration_provenance,
                &migration_record,
            )
            .await
            .unwrap();
        let migration_manual = serde_json::json!({"recipeVersion": 12, "colorPipeline": {
            "photographicRenderingModel": "legacy-oklch-v1", "manual": true}});
        catalog
            .upsert_edit_recipe_compatibility_pair(
                "migration",
                &migration_manual,
                "{\"schema\":\"recipe:v1\",\"manual\":true}",
                "migration-manual-cache",
                migration_provenance,
            )
            .await
            .unwrap();
        let stale_migration = catalog
            .accept_agent_recipe_migration_if_unchanged(
                &migration_baseline,
                &migration_legacy,
                "{\"schema\":\"recipe:v1\",\"migration\":true}",
                "migration-cache",
                migration_provenance,
                &migration_record,
            )
            .await
            .unwrap_err();
        assert!(stale_migration.to_string().contains("snapshot changed"));
        let migration_current = catalog
            .agent_asset_recipe_snapshot("migration", migration_source.to_str().unwrap())
            .await
            .unwrap();
        assert_eq!(
            migration_current.legacy_recipe.unwrap()["colorPipeline"]["manual"],
            true
        );
        assert_eq!(
            migration_current.canonical_recipe.unwrap().cache_key,
            "migration-manual-cache"
        );

        sqlx::query("DELETE FROM edit_recipes WHERE asset_id = 'manual'")
            .execute(&pool)
            .await
            .unwrap();
        let row_checkpoint = catalog
            .agent_catalog_row_snapshot(second_source.to_str().unwrap())
            .await
            .unwrap();
        sqlx::query("UPDATE assets SET rating = 4 WHERE asset_id = 'manual'")
            .execute(&pool)
            .await
            .unwrap();
        assert!(catalog
            .remove_agent_catalog_row_if_unchanged(&row_checkpoint)
            .await
            .is_err());
        assert_eq!(
            catalog
                .agent_catalog_row_snapshot(second_source.to_str().unwrap())
                .await
                .unwrap()
                .state,
            "present"
        );
        let row_checkpoint = catalog
            .agent_catalog_row_snapshot(second_source.to_str().unwrap())
            .await
            .unwrap();
        fs::write(&second_source, b"manual source changed").unwrap();
        assert!(catalog
            .remove_agent_catalog_row_if_unchanged(&row_checkpoint)
            .await
            .is_err());
        assert!(catalog
            .agent_asset_recipe_snapshot("manual", second_source.to_str().unwrap())
            .await
            .is_ok());
        sqlx::query(&format!("DROP SCHEMA {schema} CASCADE"))
            .execute(&pool)
            .await
            .unwrap();
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

#[cfg(test)]
mod agent_checkpoint_hash_performance {
    use super::sha256_source_file;
    use std::io::{Seek, SeekFrom, Write};

    #[test]
    fn exact_hash_checks_source_content_across_read_chunks() {
        let mut source = tempfile::NamedTempFile::new().expect("temporary source");
        source.write_all(b"abc").expect("write source");
        assert_eq!(
            sha256_source_file(source.path()).expect("hash source"),
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
        );
        let mut content = vec![b'a'; 1024 * 1024 + 73];
        source.seek(SeekFrom::Start(0)).expect("rewind source");
        source
            .write_all(&content)
            .expect("write multi-chunk source");
        let before = sha256_source_file(source.path()).expect("hash original");
        content[1024 * 1024] = b'b';
        source.seek(SeekFrom::Start(0)).expect("rewind source");
        source
            .write_all(&content)
            .expect("change same-length source");
        assert_ne!(
            before,
            sha256_source_file(source.path()).expect("hash changed")
        );
    }

    #[test]
    #[ignore = "read-only source hashing performance measurement"]
    fn agent_source_hash_timing() {
        let path = std::path::PathBuf::from(
            std::env::var_os("PHOTO_EDITOR_HASH_PROOF_SOURCE").expect("explicit proof source"),
        );
        let bytes = std::fs::metadata(&path).expect("source metadata").len();
        let mut expected = None;
        for iteration in 0..3 {
            let started = std::time::Instant::now();
            let digest = sha256_source_file(&path).expect("exact source hash");
            let elapsed_ms = started.elapsed().as_secs_f64() * 1000.0;
            if let Some(previous) = &expected {
                assert_eq!(previous, &digest);
            }
            expected = Some(digest.clone());
            println!(
                "hash_proof {}",
                serde_json::json!({
                    "iteration": iteration, "bytes": bytes, "elapsedMs": elapsed_ms,
                    "sha256": digest, "recipeGuard": "exact-content-rehashed"
                })
            );
        }
    }
}
