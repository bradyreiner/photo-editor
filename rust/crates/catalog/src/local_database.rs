use std::fs;
use std::io;
use std::net::TcpListener;
use std::path::{Path, PathBuf};
use std::process::Command;
use std::time::Duration;

use getrandom::getrandom;
use serde::{Deserialize, Serialize};
use sqlx::postgres::PgPoolOptions;
use sqlx::{query, query_scalar};
use time::OffsetDateTime;

use crate::{AppRuntimePaths, CatalogService, PostgresCatalogConfig};

pub const POSTGRES_BIN_DIR_ENV: &str = "PHOTO_EDITOR_POSTGRES_BIN_DIR";

const LOCAL_DATABASE_NAME: &str = "photo_editor";
const LOCAL_DATABASE_USER: &str = "photo_editor";
const LOCAL_DATABASE_HOST: &str = "127.0.0.1";
const LOCAL_POSTGRES_DIR: &str = "postgres";
const LOCAL_POSTGRES_DATA_DIR: &str = "data";
const LOCAL_POSTGRES_LOG_DIR: &str = "logs";
const LOCAL_POSTGRES_LOG_FILE: &str = "postgres.log";
const LOCAL_POSTGRES_STATE_FILE: &str = "state.json";
const LOCAL_POSTGRES_STATE_VERSION: u32 = 1;
const LOCAL_DATABASE_HEALTHCHECK_TIMEOUT: Duration = Duration::from_millis(750);

type LocalDatabaseResult<T> = Result<T, Box<dyn std::error::Error + Send + Sync>>;

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub struct LocalPostgresState {
    pub version: u32,
    pub host: String,
    pub port: u16,
    pub database_name: String,
    pub user_name: String,
    pub password: String,
    pub initialized_at: String,
}

impl LocalPostgresState {
    pub fn database_url(&self) -> String {
        format!(
            "postgresql://{}:{}@{}:{}/{}",
            self.user_name, self.password, self.host, self.port, self.database_name
        )
    }

    pub fn maintenance_database_url(&self) -> String {
        format!(
            "postgresql://{}:{}@{}:{}/postgres",
            self.user_name, self.password, self.host, self.port
        )
    }

    pub fn redacted_database_url(&self) -> String {
        format!(
            "postgresql://{}:<redacted>@{}:{}/{}",
            self.user_name, self.host, self.port, self.database_name
        )
    }
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub struct CatalogDatabaseStatus {
    pub mode: String,
    pub database_url: String,
    pub healthy: bool,
    pub initialized: bool,
    pub state_file: Option<PathBuf>,
    pub data_dir: Option<PathBuf>,
    pub log_file: Option<PathBuf>,
}

pub struct LocalCatalogDatabaseManager {
    runtime_paths: AppRuntimePaths,
}

impl LocalCatalogDatabaseManager {
    pub fn from_env() -> Self {
        Self {
            runtime_paths: AppRuntimePaths::from_env(),
        }
    }

    pub async fn ensure_ready_database_url(&self) -> LocalDatabaseResult<String> {
        self.runtime_paths.ensure_directories()?;

        if let Ok(database_url) = std::env::var("PHOTO_EDITOR_DATABASE_URL") {
            ensure_external_database_ready(&database_url).await?;
            return Ok(database_url);
        }

        let state = self.ensure_local_database_ready().await?;
        Ok(state.database_url())
    }

    pub async fn status(&self) -> LocalDatabaseResult<CatalogDatabaseStatus> {
        self.runtime_paths.ensure_directories()?;

        if let Ok(database_url) = std::env::var("PHOTO_EDITOR_DATABASE_URL") {
            let healthy = database_healthcheck(&database_url).await;
            return Ok(CatalogDatabaseStatus {
                mode: "external".to_string(),
                database_url: redact_connection_url(&database_url),
                healthy,
                initialized: true,
                state_file: None,
                data_dir: None,
                log_file: None,
            });
        }

        let initialized =
            self.cluster_data_dir().join("PG_VERSION").exists() && self.state_file_path().exists();
        let status_state = self.load_state().ok();
        let healthy = match &status_state {
            Some(state) => database_healthcheck(&state.database_url()).await,
            None => false,
        };

        Ok(CatalogDatabaseStatus {
            mode: "local-managed".to_string(),
            database_url: status_state
                .map(|state| state.redacted_database_url())
                .unwrap_or_default(),
            healthy,
            initialized,
            state_file: Some(self.state_file_path()),
            data_dir: Some(self.cluster_data_dir()),
            log_file: Some(self.cluster_log_file()),
        })
    }

    pub fn stop(&self) -> LocalDatabaseResult<()> {
        if std::env::var("PHOTO_EDITOR_DATABASE_URL").is_ok() {
            return Ok(());
        }

        let data_dir = self.cluster_data_dir();
        if !data_dir.join("PG_VERSION").exists() {
            return Ok(());
        }

        let status = Command::new(self.postgres_executable("pg_ctl"))
            .arg("-D")
            .arg(&data_dir)
            .arg("status")
            .output()?;
        if !status.status.success() {
            return Ok(());
        }

        self.run_postgres_command(
            "pg_ctl",
            &[
                "-D".to_string(),
                data_dir.display().to_string(),
                "-m".to_string(),
                "fast".to_string(),
                "stop".to_string(),
            ],
        )?;
        Ok(())
    }

    fn service_root(&self) -> PathBuf {
        self.runtime_paths.app_data_dir.join(LOCAL_POSTGRES_DIR)
    }

    fn cluster_data_dir(&self) -> PathBuf {
        self.service_root().join(LOCAL_POSTGRES_DATA_DIR)
    }

    fn cluster_log_dir(&self) -> PathBuf {
        self.service_root().join(LOCAL_POSTGRES_LOG_DIR)
    }

    fn cluster_log_file(&self) -> PathBuf {
        self.cluster_log_dir().join(LOCAL_POSTGRES_LOG_FILE)
    }

    fn state_file_path(&self) -> PathBuf {
        self.service_root().join(LOCAL_POSTGRES_STATE_FILE)
    }

    async fn ensure_local_database_ready(&self) -> LocalDatabaseResult<LocalPostgresState> {
        fs::create_dir_all(self.service_root())?;
        fs::create_dir_all(self.cluster_log_dir())?;

        let state = self.load_or_create_state()?;
        self.initialize_cluster_if_needed(&state)?;

        if !database_healthcheck(&state.maintenance_database_url()).await {
            self.start_cluster(&state)?;
        }

        self.ensure_database_exists(&state).await?;

        let config = PostgresCatalogConfig {
            database_url: state.database_url(),
            max_connections: 2,
        };
        let catalog = CatalogService::connect(&config).await?;
        catalog.initialize_schema().await?;
        catalog.healthcheck().await?;

        Ok(state)
    }

    async fn ensure_database_exists(&self, state: &LocalPostgresState) -> LocalDatabaseResult<()> {
        if database_healthcheck(&state.database_url()).await {
            return Ok(());
        }

        let maintenance_config = PostgresCatalogConfig {
            database_url: state.maintenance_database_url(),
            max_connections: 1,
        };
        let maintenance_catalog = CatalogService::connect(&maintenance_config).await?;
        let exists: bool =
            query_scalar("SELECT EXISTS(SELECT 1 FROM pg_database WHERE datname = $1)")
                .bind(&state.database_name)
                .fetch_one(maintenance_catalog.pool())
                .await?;

        if !exists {
            query(&format!(
                "CREATE DATABASE {}",
                quote_identifier(&state.database_name)
            ))
            .execute(maintenance_catalog.pool())
            .await?;
        }

        Ok(())
    }

    fn initialize_cluster_if_needed(&self, state: &LocalPostgresState) -> LocalDatabaseResult<()> {
        let data_dir = self.cluster_data_dir();
        if data_dir.join("PG_VERSION").exists() {
            return Ok(());
        }

        fs::create_dir_all(&data_dir)?;
        let password_file = self.service_root().join(".bootstrap-password");
        write_secret_file(&password_file, &format!("{}\n", state.password))?;

        let init_result = self.run_postgres_command(
            "initdb",
            &[
                "--pgdata".to_string(),
                data_dir.display().to_string(),
                "--username".to_string(),
                state.user_name.clone(),
                "--auth-host".to_string(),
                "scram-sha-256".to_string(),
                "--auth-local".to_string(),
                "trust".to_string(),
                "--pwfile".to_string(),
                password_file.display().to_string(),
            ],
        );

        let _ = fs::remove_file(password_file);
        init_result
    }

    fn start_cluster(&self, state: &LocalPostgresState) -> LocalDatabaseResult<()> {
        self.run_postgres_command(
            "pg_ctl",
            &[
                "-D".to_string(),
                self.cluster_data_dir().display().to_string(),
                "-l".to_string(),
                self.cluster_log_file().display().to_string(),
                "-w".to_string(),
                "start".to_string(),
                "-o".to_string(),
                format!(
                    "-c listen_addresses={} -c port={} -c max_connections=32",
                    state.host, state.port
                ),
            ],
        )
    }

    fn load_or_create_state(&self) -> LocalDatabaseResult<LocalPostgresState> {
        match self.load_state() {
            Ok(state) => Ok(state),
            Err(_) => {
                let state = LocalPostgresState {
                    version: LOCAL_POSTGRES_STATE_VERSION,
                    host: LOCAL_DATABASE_HOST.to_string(),
                    port: pick_available_port()?,
                    database_name: LOCAL_DATABASE_NAME.to_string(),
                    user_name: LOCAL_DATABASE_USER.to_string(),
                    password: generate_secret(24)?,
                    initialized_at: OffsetDateTime::now_utc()
                        .format(&time::format_description::well_known::Rfc3339)?,
                };
                self.save_state(&state)?;
                Ok(state)
            }
        }
    }

    fn load_state(&self) -> LocalDatabaseResult<LocalPostgresState> {
        let contents = fs::read_to_string(self.state_file_path())?;
        Ok(serde_json::from_str(&contents)?)
    }

    fn save_state(&self, state: &LocalPostgresState) -> LocalDatabaseResult<()> {
        let json = serde_json::to_string_pretty(state)?;
        write_secret_file(&self.state_file_path(), &json)?;
        Ok(())
    }

    fn postgres_executable(&self, name: &str) -> PathBuf {
        let executable_name = executable_name(name);
        if let Some(bin_dir) = explicit_postgres_bin_dir() {
            return bin_dir.join(&executable_name);
        }

        if let Some(runtime_root) = bundled_postgres_runtime_root() {
            return runtime_root.join("bin").join(&executable_name);
        }

        PathBuf::from(executable_name)
    }

    fn run_postgres_command(&self, name: &str, args: &[String]) -> LocalDatabaseResult<()> {
        let mut command = Command::new(self.postgres_executable(name));
        command.args(args);
        apply_runtime_environment(&mut command);

        let output = command.output()?;
        if output.status.success() {
            return Ok(());
        }

        Err(io::Error::other(format!(
            "{} failed with code {:?}: {}",
            name,
            output.status.code(),
            String::from_utf8_lossy(&output.stderr).trim()
        ))
        .into())
    }
}

pub async fn ensure_external_database_ready(database_url: &str) -> LocalDatabaseResult<()> {
    let config = PostgresCatalogConfig {
        database_url: database_url.to_string(),
        max_connections: 2,
    };
    let catalog = CatalogService::connect(&config).await?;
    catalog.initialize_schema().await?;
    catalog.healthcheck().await?;
    Ok(())
}

async fn database_healthcheck(database_url: &str) -> bool {
    let pool = match PgPoolOptions::new()
        .max_connections(1)
        .acquire_timeout(LOCAL_DATABASE_HEALTHCHECK_TIMEOUT)
        .connect(database_url)
        .await
    {
        Ok(pool) => pool,
        Err(_) => return false,
    };

    query("SELECT 1").execute(&pool).await.is_ok()
}

#[cfg(test)]
async fn database_healthcheck_with_timeout(database_url: &str, timeout: Duration) -> bool {
    match PgPoolOptions::new()
        .max_connections(1)
        .acquire_timeout(timeout)
        .connect(database_url)
        .await
    {
        Ok(pool) => query("SELECT 1").execute(&pool).await.is_ok(),
        Err(_) => false,
    }
}

fn generate_secret(byte_count: usize) -> LocalDatabaseResult<String> {
    let mut bytes = vec![0_u8; byte_count];
    getrandom(&mut bytes)?;
    Ok(bytes_to_hex(&bytes))
}

fn bytes_to_hex(bytes: &[u8]) -> String {
    let mut output = String::with_capacity(bytes.len() * 2);
    for byte in bytes {
        output.push(hex_digit(byte >> 4));
        output.push(hex_digit(byte & 0x0f));
    }
    output
}

fn hex_digit(value: u8) -> char {
    match value {
        0..=9 => (b'0' + value) as char,
        _ => (b'a' + (value - 10)) as char,
    }
}

fn pick_available_port() -> LocalDatabaseResult<u16> {
    let listener = TcpListener::bind((LOCAL_DATABASE_HOST, 0))?;
    let port = listener.local_addr()?.port();
    drop(listener);
    Ok(port)
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

fn write_secret_file(path: &Path, contents: &str) -> io::Result<()> {
    if let Some(parent) = path.parent() {
        fs::create_dir_all(parent)?;
    }

    fs::write(path, contents)?;

    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        fs::set_permissions(path, fs::Permissions::from_mode(0o600))?;
    }

    Ok(())
}

fn executable_name(name: &str) -> String {
    #[cfg(target_os = "windows")]
    {
        format!("{name}.exe")
    }

    #[cfg(not(target_os = "windows"))]
    {
        name.to_string()
    }
}

fn explicit_postgres_bin_dir() -> Option<PathBuf> {
    std::env::var_os(POSTGRES_BIN_DIR_ENV).map(PathBuf::from)
}

fn bundled_postgres_runtime_root() -> Option<PathBuf> {
    let executable_path = std::env::current_exe().ok()?;
    let executable_dir = executable_path.parent()?;
    let candidates = [
        executable_dir.join("postgres"),
        executable_dir.join("../postgres"),
        executable_dir.join("../Resources/postgres"),
    ];

    candidates.into_iter().find(|candidate| {
        candidate
            .join("bin")
            .join(executable_name("pg_ctl"))
            .exists()
    })
}

fn apply_runtime_environment(command: &mut Command) {
    if let Some(bin_dir) = explicit_postgres_bin_dir() {
        prepend_path_env(command, "PATH", &bin_dir);
        return;
    }

    if let Some(runtime_root) = bundled_postgres_runtime_root() {
        let bin_dir = runtime_root.join("bin");
        prepend_path_env(command, "PATH", &bin_dir);

        let lib_dir = runtime_root.join("lib");
        if lib_dir.exists() {
            #[cfg(target_os = "windows")]
            prepend_path_env(command, "PATH", &lib_dir);

            #[cfg(target_os = "macos")]
            prepend_path_env(command, "DYLD_FALLBACK_LIBRARY_PATH", &lib_dir);

            #[cfg(all(unix, not(target_os = "macos")))]
            prepend_path_env(command, "LD_LIBRARY_PATH", &lib_dir);
        }
    }
}

fn prepend_path_env(command: &mut Command, key: &str, value: &Path) {
    let separator = if cfg!(target_os = "windows") {
        ';'
    } else {
        ':'
    };
    let prefix = value.display().to_string();
    let combined = match std::env::var_os(key) {
        Some(existing) if !existing.is_empty() => {
            format!("{prefix}{separator}{}", PathBuf::from(existing).display())
        }
        _ => prefix,
    };
    command.env(key, combined);
}

fn quote_identifier(value: &str) -> String {
    format!("\"{}\"", value.replace('"', "\"\""))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn local_state_builds_expected_urls() {
        let state = LocalPostgresState {
            version: 1,
            host: "127.0.0.1".to_string(),
            port: 15432,
            database_name: "photo_editor".to_string(),
            user_name: "photo_editor".to_string(),
            password: "secret".to_string(),
            initialized_at: "2026-03-26T00:00:00Z".to_string(),
        };

        assert_eq!(
            state.database_url(),
            "postgresql://photo_editor:secret@127.0.0.1:15432/photo_editor"
        );
        assert_eq!(
            state.redacted_database_url(),
            "postgresql://photo_editor:<redacted>@127.0.0.1:15432/photo_editor"
        );
    }

    #[test]
    fn redaction_hides_passwords() {
        assert_eq!(
            redact_connection_url("postgresql://user:secret@localhost:5432/db"),
            "postgresql://user:<redacted>@localhost:5432/db"
        );
    }

    #[tokio::test]
    async fn local_healthcheck_fails_fast_for_unavailable_database() {
        let started_at = std::time::Instant::now();
        let healthy = database_healthcheck_with_timeout(
            &format!("postgresql://photo_editor:secret@{LOCAL_DATABASE_HOST}:1/photo_editor"),
            Duration::from_millis(75),
        )
        .await;

        assert!(!healthy);
        assert!(
            started_at.elapsed() < Duration::from_secs(2),
            "healthcheck should not wait for SQLx's default connection timeout"
        );
    }
}
