mod importer;

use serde::{Deserialize, Serialize};
use serde_json::Value;
use sqlx::postgres::{PgPoolOptions, PgRow};
use sqlx::{Executor, PgPool, Row};

pub use importer::{
    discover_assets, extract_metadata, fallback_metadata, is_supported_photo_path, ExtractedMetadata,
    ImportRequest, ImportSummary,
};

const DEFAULT_DATABASE_URL: &str = "postgresql://photo_editor:photo_editor@localhost:5433/photo_editor";
const DEFAULT_MAX_CONNECTIONS: u32 = 10;
const POSTGRES_SCHEMA_SQL: &str = include_str!("../../../../docs/postgres-schema.sql");

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

    pub async fn initialize_schema(&self) -> Result<(), sqlx::Error> {
        self.pool.execute(POSTGRES_SCHEMA_SQL).await?;
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
}
