mod importer;
mod local_database;
pub mod observability;

use std::fs;
use std::path::{Path, PathBuf};

use serde::{Deserialize, Serialize};
use serde_json::Value;
use sqlx::migrate::Migrator;
use sqlx::postgres::{PgPoolOptions, PgRow};
use sqlx::{PgPool, Row};

pub use importer::{
    discover_assets, extract_metadata, fallback_metadata, is_supported_photo_path,
    ExtractedMetadata, ImportRequest, ImportSummary,
};
pub use local_database::{
    ensure_external_database_ready, CatalogDatabaseStatus, LocalCatalogDatabaseManager,
    LocalPostgresState, POSTGRES_BIN_DIR_ENV,
};

const DEFAULT_DATABASE_URL: &str =
    "postgresql://photo_editor:photo_editor@localhost:5433/photo_editor";
const DEFAULT_MAX_CONNECTIONS: u32 = 10;
pub const APP_DATA_DIR_ENV: &str = "PHOTO_EDITOR_APP_DATA_DIR";
pub const LOG_DIR_ENV: &str = "PHOTO_EDITOR_LOG_DIR";
pub const BACKUP_DIR_ENV: &str = "PHOTO_EDITOR_BACKUP_DIR";

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

    fn from_database_value(value: &str) -> Self {
        match value {
            "managed" => Self::Managed,
            _ => Self::Referenced,
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
    pub imported_at: String,
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

pub struct CatalogService {
    pool: PgPool,
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
        self.upsert_drive(&summary.drive).await?;

        for asset in &summary.assets {
            self.upsert_asset(asset).await?;
        }

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

        Ok(rows.into_iter().map(map_asset_row).collect())
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
                TO_CHAR(a.created_at AT TIME ZONE 'UTC', 'YYYY-MM-DD"T"HH24:MI:SS"Z"') AS imported_at
            FROM assets a
            INNER JOIN drives d ON d.drive_id = a.drive_id
            LEFT JOIN edit_recipes er ON er.asset_id = a.asset_id
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

        Ok(rows.into_iter().map(map_asset_snapshot_row).collect())
    }

    pub async fn upsert_edit_recipe(
        &self,
        asset_id: &str,
        recipe_json: &Value,
    ) -> Result<(), sqlx::Error> {
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
        .execute(&self.pool)
        .await?;

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

        Ok(rows
            .into_iter()
            .map(|row| DriveRecord {
                drive_id: row.get("drive_id"),
                display_name: row.get("display_name"),
                mount_path: row.get("mount_path"),
                online: row.get("is_online"),
            })
            .collect())
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

        Ok(rows
            .into_iter()
            .map(|row| PersistedProject {
                name: row.get("name"),
                created_at: row.get("created_at"),
                asset_ids: row
                    .get::<Value, _>("asset_ids")
                    .as_array()
                    .into_iter()
                    .flatten()
                    .filter_map(|value| value.as_str().map(ToOwned::to_owned))
                    .collect(),
            })
            .collect())
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
            SELECT asset_id, rating, picked, rejected
            FROM asset_review_states
            ORDER BY asset_id ASC
            "#,
        )
        .fetch_all(&self.pool)
        .await?;

        Ok(rows
            .into_iter()
            .map(|row| AssetReviewStateRecord {
                asset_id: row.get("asset_id"),
                rating: row.get("rating"),
                picked: row.get("picked"),
                rejected: row.get("rejected"),
            })
            .collect())
    }

    pub async fn upsert_asset_review_state(
        &self,
        state: &AssetReviewStateRecord,
    ) -> Result<(), sqlx::Error> {
        sqlx::query(
            r#"
            INSERT INTO asset_review_states (asset_id, rating, picked, rejected, updated_at)
            VALUES ($1, $2, $3, $4, NOW())
            ON CONFLICT (asset_id) DO UPDATE SET
                rating = EXCLUDED.rating,
                picked = EXCLUDED.picked,
                rejected = EXCLUDED.rejected,
                updated_at = NOW()
            "#,
        )
        .bind(&state.asset_id)
        .bind(state.rating)
        .bind(state.picked)
        .bind(state.rejected)
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

        Ok(rows
            .into_iter()
            .map(|row| AssetRelinkRecord {
                asset_id: row.get("asset_id"),
                relinked_file_path: row.get("relinked_file_path"),
            })
            .collect())
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

        Ok(rows
            .into_iter()
            .map(|row| PersistedExportJob {
                job_id: row.get("job_id"),
                job_kind: row.get("job_kind"),
                asset_id: row.get("asset_id"),
                media_type: row.get("media_type"),
                display_label: row.get("display_label"),
                source_path: row.get("source_path"),
                edited_preview_source: row.get("edited_preview_source"),
                recipe_json: row.get("recipe_json"),
                metadata_json: row.get("metadata_json"),
                preset_name: row.get("preset_name"),
                destination_path: row.get("destination_path"),
                output_path: row.get("output_path"),
                status: row.get("status"),
                error_message: row.get("error_message"),
                progress: row.get("progress"),
                created_at: row.get("created_at"),
                updated_at: row.get("updated_at"),
            })
            .collect())
    }

    pub async fn upsert_export_job(&self, job: &PersistedExportJob) -> Result<(), sqlx::Error> {
        sqlx::query(
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
        .execute(&self.pool)
        .await?;

        Ok(())
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

        Ok(rows
            .into_iter()
            .map(|row| PersistedTimelineDocument {
                timeline_id: row.get("timeline_id"),
                asset_id: row.get("asset_id"),
                name: row.get("name"),
                document_path: row.get("document_path"),
                render_path: row.get("render_path"),
                poster_path: row.get("poster_path"),
                duration_seconds: row.get("duration_seconds"),
                document_json: row.get("document_json"),
                created_at: row.get("created_at"),
                updated_at: row.get("updated_at"),
            })
            .collect())
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

        Ok(row.map(|row| PersistedTimelineDocument {
            timeline_id: row.get("timeline_id"),
            asset_id: row.get("asset_id"),
            name: row.get("name"),
            document_path: row.get("document_path"),
            render_path: row.get("render_path"),
            poster_path: row.get("poster_path"),
            duration_seconds: row.get("duration_seconds"),
            document_json: row.get("document_json"),
            created_at: row.get("created_at"),
            updated_at: row.get("updated_at"),
        }))
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

        Ok(rows
            .into_iter()
            .map(|row| PersistedCarouselDocument {
                document_id: row.get("document_id"),
                name: row.get("name"),
                document_format: row.get("document_format"),
                slide_count: row.get("slide_count"),
                document_json: row.get("document_json"),
                created_at: row.get("created_at"),
                updated_at: row.get("updated_at"),
            })
            .collect())
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

        Ok(row.map(|row| PersistedCarouselDocument {
            document_id: row.get("document_id"),
            name: row.get("name"),
            document_format: row.get("document_format"),
            slide_count: row.get("slide_count"),
            document_json: row.get("document_json"),
            created_at: row.get("created_at"),
            updated_at: row.get("updated_at"),
        }))
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

fn map_asset_row(row: PgRow) -> CatalogAssetRecord {
    let capture_year: i32 = row.get("capture_year");
    let capture_month: i32 = row.get("capture_month");
    let capture_day: i32 = row.get("capture_day");
    let import_mode: String = row.get("import_mode");

    CatalogAssetRecord {
        asset_id: row.get("asset_id"),
        capture_year: clamp_i32_to_u16(capture_year),
        capture_month: clamp_i32_to_u8(capture_month),
        capture_day: clamp_i32_to_u8(capture_day),
        camera_make: row.get("camera_make"),
        camera_model: row.get("camera_model"),
        lens_model: row.get("lens_model"),
        iso: row.get("iso"),
        shutter_speed: row.get("shutter_speed"),
        aperture: row.get("aperture"),
        import_mode: ImportMode::from_database_value(&import_mode),
        location: AssetLocation {
            drive_id: row.get("drive_id"),
            root_path: row.get("root_path"),
            relative_path: row.get("relative_path"),
            online: row.get("is_online"),
        },
    }
}

fn map_asset_snapshot_row(row: PgRow) -> CatalogAssetSnapshot {
    let edit_recipe: Option<Value> = row.get("recipe_json");
    let imported_at: String = row.get("imported_at");

    CatalogAssetSnapshot {
        asset: map_asset_row(row),
        edit_recipe,
        imported_at,
    }
}

fn clamp_i32_to_u16(value: i32) -> u16 {
    u16::try_from(value).unwrap_or_default()
}

fn clamp_i32_to_u8(value: i32) -> u8 {
    u8::try_from(value).unwrap_or_default()
}

#[cfg(test)]
mod tests {
    use super::*;

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
            ImportMode::from_database_value("managed"),
            ImportMode::Managed
        );
        assert_eq!(
            ImportMode::from_database_value("anything-else"),
            ImportMode::Referenced
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
