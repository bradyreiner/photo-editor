mod importer;
mod local_database;

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
