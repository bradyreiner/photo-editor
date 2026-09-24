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
const LOCAL_POSTGRES_LEGACY_CREDENTIAL_FILE: &str = ".credential";
const LOCAL_POSTGRES_STATE_VERSION: u32 = 2;
const LOCAL_DATABASE_HEALTHCHECK_TIMEOUT: Duration = Duration::from_millis(750);

type LocalDatabaseResult<T> = Result<T, Box<dyn std::error::Error + Send + Sync>>;

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub struct LocalPostgresState {
    pub version: u32,
    pub host: String,
    pub port: u16,
    pub database_name: String,
    pub user_name: String,
    #[serde(default)]
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

    pub async fn backup_to(&self, destination: &Path) -> LocalDatabaseResult<()> {
        if destination.exists() {
            return Err(io::Error::new(
                io::ErrorKind::AlreadyExists,
                format!(
                    "PostgreSQL backup destination already exists: {}",
                    destination.display()
                ),
            )
            .into());
        }
        if let Some(parent) = destination.parent() {
            fs::create_dir_all(parent)?;
        }
        let database_url = self.ensure_ready_database_url().await?;
        self.write_backup_archive(&database_url, destination)
    }

    fn write_backup_archive(
        &self,
        database_url: &str,
        destination: &Path,
    ) -> LocalDatabaseResult<()> {
        let mut command = Command::new(self.postgres_executable("pg_dump"));
        command
            .arg("--format=custom")
            .arg("--no-owner")
            .arg("--no-privileges")
            .arg("--dbname")
            .arg(database_url)
            .arg("--file")
            .arg(destination.with_extension("dump.partial"));
        self.publish_backup_archive(destination, command, "pg_dump failed")
    }

    fn publish_backup_archive(
        &self,
        destination: &Path,
        mut command: Command,
        failure_context: &str,
    ) -> LocalDatabaseResult<()> {
        if destination.exists() {
            return Err(io::Error::new(
                io::ErrorKind::AlreadyExists,
                format!(
                    "PostgreSQL backup destination already exists: {}",
                    destination.display()
                ),
            )
            .into());
        }
        if let Some(parent) = destination.parent() {
            fs::create_dir_all(parent)?;
        }
        let partial_path = destination.with_extension("dump.partial");
        if partial_path.exists() {
            fs::remove_file(&partial_path)?;
        }
        apply_runtime_environment(&mut command);
        let output = match command.output() {
            Ok(output) => output,
            Err(error) => {
                let _ = fs::remove_file(&partial_path);
                return Err(error.into());
            }
        };
        if !output.status.success() {
            let _ = fs::remove_file(&partial_path);
            return Err(io::Error::other(format!(
                "{failure_context}: {}",
                String::from_utf8_lossy(&output.stderr).trim()
            ))
            .into());
        }
        #[cfg(unix)]
        {
            use std::os::unix::fs::PermissionsExt;
            if let Err(error) =
                fs::set_permissions(&partial_path, fs::Permissions::from_mode(0o600))
            {
                let _ = fs::remove_file(&partial_path);
                return Err(error.into());
            }
        }
        if let Err(error) = fs::hard_link(&partial_path, destination) {
            let _ = fs::remove_file(&partial_path);
            if destination.exists() {
                return Err(io::Error::new(
                    io::ErrorKind::AlreadyExists,
                    format!(
                        "PostgreSQL backup destination already exists: {}",
                        destination.display()
                    ),
                )
                .into());
            }
            return Err(error.into());
        }
        let _ = fs::remove_file(&partial_path);
        Ok(())
    }

    pub async fn backup_now(&self) -> LocalDatabaseResult<PathBuf> {
        self.runtime_paths.ensure_directories()?;
        let destination = self
            .runtime_paths
            .backup_dir
            .join(format!("catalog-manual-{}.dump", backup_timestamp()?));
        self.backup_to(&destination).await?;
        Ok(destination)
    }

    pub async fn restore_latest_backup(&self) -> LocalDatabaseResult<PathBuf> {
        if std::env::var("PHOTO_EDITOR_DATABASE_URL").is_ok() {
            return Err(io::Error::new(
                io::ErrorKind::PermissionDenied,
                "automatic restore is disabled for an external PostgreSQL server; use an administrator-reviewed pg_restore workflow",
            )
            .into());
        }
        self.runtime_paths.ensure_directories()?;
        let candidate =
            latest_restorable_backup(&self.runtime_paths.backup_dir)?.ok_or_else(|| {
                io::Error::new(
                    io::ErrorKind::NotFound,
                    "no PostgreSQL catalog backup is available to restore",
                )
            })?;
        self.validate_backup(&candidate)?;

        let database_url = self.ensure_ready_database_url().await?;
        let safety_backup = self
            .runtime_paths
            .backup_dir
            .join(format!("pre-restore-{}.dump", backup_timestamp()?));
        self.backup_to(&safety_backup).await?;

        if let Err(restore_error) = self.restore_from(&database_url, &candidate) {
            let rollback_result = self.restore_from(&database_url, &safety_backup);
            return match rollback_result {
                Ok(()) => Err(io::Error::other(format!(
                    "PostgreSQL restore failed and the pre-restore safety backup was reapplied: {restore_error}"
                ))
                .into()),
                Err(rollback_error) => Err(io::Error::other(format!(
                    "PostgreSQL restore failed ({restore_error}) and rollback also failed ({rollback_error}); safety backup: {}",
                    safety_backup.display()
                ))
                .into()),
            };
        }

        ensure_external_database_ready(&database_url).await?;
        Ok(candidate)
    }

    fn validate_backup(&self, backup: &Path) -> LocalDatabaseResult<()> {
        let mut command = Command::new(self.postgres_executable("pg_restore"));
        command.arg("--list").arg(backup);
        apply_runtime_environment(&mut command);
        let output = command.output()?;
        if output.status.success() {
            return Ok(());
        }
        Err(io::Error::new(
            io::ErrorKind::InvalidData,
            format!(
                "PostgreSQL backup validation failed: {}",
                String::from_utf8_lossy(&output.stderr).trim()
            ),
        )
        .into())
    }

    fn restore_from(&self, database_url: &str, backup: &Path) -> LocalDatabaseResult<()> {
        let mut command = Command::new(self.postgres_executable("pg_restore"));
        command
            .arg("--clean")
            .arg("--if-exists")
            .arg("--exit-on-error")
            .arg("--no-owner")
            .arg("--no-privileges")
            .arg("--dbname")
            .arg(database_url)
            .arg(backup);
        apply_runtime_environment(&mut command);
        let output = command.output()?;
        if output.status.success() {
            return Ok(());
        }
        Err(io::Error::other(format!(
            "pg_restore failed: {}",
            String::from_utf8_lossy(&output.stderr).trim()
        ))
        .into())
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

    fn legacy_credential_file_path(&self) -> PathBuf {
        self.service_root()
            .join(LOCAL_POSTGRES_LEGACY_CREDENTIAL_FILE)
    }

    async fn ensure_local_database_ready(&self) -> LocalDatabaseResult<LocalPostgresState> {
        fs::create_dir_all(self.service_root())?;
        fs::create_dir_all(self.cluster_log_dir())?;

        let mut state = self.load_or_create_state()?;
        self.initialize_cluster_if_needed(&state)?;

        state = self.recover_legacy_credential_if_needed(state).await?;
        if !database_healthcheck(&state.maintenance_database_url()).await {
            // A short SQL probe can time out under load while the postmaster is
            // healthy. Do not try to start a second server in its data directory.
            // The normal catalog connection below still validates credentials
            // and database readiness; a running process alone is not success.
            let status = Command::new(self.postgres_executable("pg_ctl"))
                .arg("-D")
                .arg(self.cluster_data_dir())
                .arg("status")
                .output()?;
            if !status.status.success() {
                self.start_cluster(&state)?;
                state = self.recover_legacy_credential_if_needed(state).await?;
            }
        }

        self.ensure_database_exists(&state).await?;

        let config = PostgresCatalogConfig {
            database_url: state.database_url(),
            max_connections: 2,
        };
        let catalog = CatalogService::connect(&config).await?;
        self.backup_before_schema_migration(&state, &catalog)
            .await?;
        catalog.initialize_schema().await?;
        catalog.healthcheck().await?;

        Ok(state)
    }

    async fn backup_before_schema_migration(
        &self,
        state: &LocalPostgresState,
        catalog: &CatalogService,
    ) -> LocalDatabaseResult<()> {
        if std::env::var("PHOTO_EDITOR_REQUIRE_PRE_MIGRATION_BACKUP")
            .map(|value| value == "0" || value.eq_ignore_ascii_case("false"))
            .unwrap_or(false)
        {
            return Ok(());
        }

        let has_existing_schema: bool = query_scalar(
            "SELECT EXISTS(SELECT 1 FROM information_schema.tables WHERE table_schema = 'public' AND table_name = '_sqlx_migrations')",
        )
        .fetch_one(catalog.pool())
        .await?;
        if !has_existing_schema {
            return Ok(());
        }

        let app_version = std::env::var("PHOTO_EDITOR_APP_VERSION")
            .unwrap_or_else(|_| env!("CARGO_PKG_VERSION").to_string())
            .chars()
            .map(|character| {
                if character.is_ascii_alphanumeric() || character == '.' || character == '-' {
                    character
                } else {
                    '_'
                }
            })
            .collect::<String>();
        let backup_prefix = format!("pre-migration-v{app_version}-");
        fs::create_dir_all(&self.runtime_paths.backup_dir)?;
        if fs::read_dir(&self.runtime_paths.backup_dir)?.any(|entry| {
            entry
                .ok()
                .and_then(|value| value.file_name().into_string().ok())
                .is_some_and(|name| name.starts_with(&backup_prefix) && name.ends_with(".dump"))
        }) {
            return Ok(());
        }

        let timestamp = OffsetDateTime::now_utc().format(&time::macros::format_description!(
            "[year][month][day]T[hour][minute][second]Z"
        ))?;
        let final_path = self
            .runtime_paths
            .backup_dir
            .join(format!("{backup_prefix}{timestamp}.dump"));
        let mut command = Command::new(self.postgres_executable("pg_dump"));
        command
            .arg("--format=custom")
            .arg("--no-owner")
            .arg("--no-privileges")
            .arg("--host")
            .arg(&state.host)
            .arg("--port")
            .arg(state.port.to_string())
            .arg("--username")
            .arg(&state.user_name)
            .arg("--dbname")
            .arg(&state.database_name)
            .arg("--file")
            .arg(final_path.with_extension("dump.partial"))
            .env("PGPASSWORD", &state.password);
        self.publish_backup_archive(
            &final_path,
            command,
            "pg_dump failed before catalog migration",
        )
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
            Err(error) => {
                if self.cluster_data_dir().join("PG_VERSION").exists() {
                    return Err(io::Error::new(
                        io::ErrorKind::InvalidData,
                        format!(
                            "managed PostgreSQL state is invalid for an existing catalog; refusing to generate replacement authority: {error}"
                        ),
                    )
                    .into());
                }
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
        let mut state: LocalPostgresState = serde_json::from_str(&contents)?;
        if state.password.is_empty() {
            state.password = self.read_legacy_credential()?;
        }
        Ok(state)
    }

    fn read_legacy_credential(&self) -> LocalDatabaseResult<String> {
        let credential = fs::read_to_string(self.legacy_credential_file_path())?
            .trim()
            .to_string();
        if credential.is_empty() {
            return Err(io::Error::new(
                io::ErrorKind::InvalidData,
                "managed PostgreSQL legacy credential file is empty",
            )
            .into());
        }
        Ok(credential)
    }

    async fn recover_legacy_credential_if_needed(
        &self,
        state: LocalPostgresState,
    ) -> LocalDatabaseResult<LocalPostgresState> {
        if database_healthcheck(&state.maintenance_database_url()).await {
            return Ok(state);
        }

        let legacy_credential = match self.read_legacy_credential() {
            Ok(credential) if credential != state.password => credential,
            Ok(_) | Err(_) => return Ok(state),
        };
        let mut recovered = state.clone();
        recovered.password = legacy_credential;
        if !database_healthcheck(&recovered.maintenance_database_url()).await {
            return Ok(state);
        }

        recovered.version = LOCAL_POSTGRES_STATE_VERSION;
        self.save_state(&recovered)?;
        Ok(recovered)
    }

    fn save_state(&self, state: &LocalPostgresState) -> LocalDatabaseResult<()> {
        // The database listens only on loopback and this credential file is owner-only.
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

fn backup_timestamp() -> LocalDatabaseResult<String> {
    Ok(
        OffsetDateTime::now_utc().format(&time::macros::format_description!(
            "[year][month][day]T[hour][minute][second][subsecond digits:3]Z"
        ))?,
    )
}

fn latest_restorable_backup(backup_dir: &Path) -> LocalDatabaseResult<Option<PathBuf>> {
    let mut candidates = fs::read_dir(backup_dir)?
        .filter_map(Result::ok)
        .filter_map(|entry| {
            let path = entry.path();
            let name = path.file_name()?.to_str()?;
            if !entry.file_type().ok()?.is_file()
                || !name.ends_with(".dump")
                || name.starts_with("pre-restore-")
            {
                return None;
            }
            let modified = entry.metadata().ok()?.modified().ok()?;
            Some((modified, path))
        })
        .collect::<Vec<_>>();
    candidates.sort_by(|left, right| right.0.cmp(&left.0));
    Ok(candidates.into_iter().next().map(|(_, path)| path))
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
    use std::sync::{Mutex, OnceLock};

    fn environment_lock() -> std::sync::MutexGuard<'static, ()> {
        static LOCK: OnceLock<Mutex<()>> = OnceLock::new();
        LOCK.get_or_init(|| Mutex::new(())).lock().unwrap()
    }

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

    #[test]
    fn legacy_owner_only_credential_recovers_passwordless_state() {
        let root = tempfile::tempdir().unwrap();
        let app_data_dir = root.path().join("app-data");
        let service_root = app_data_dir.join(LOCAL_POSTGRES_DIR);
        fs::create_dir_all(&service_root).unwrap();
        fs::write(
            service_root.join(LOCAL_POSTGRES_STATE_FILE),
            r#"{
  "version": 4,
  "host": "127.0.0.1",
  "port": 15432,
  "database_name": "photo_editor",
  "user_name": "photo_editor",
  "initialized_at": "2026-03-26T00:00:00Z"
}"#,
        )
        .unwrap();
        write_secret_file(
            &service_root.join(LOCAL_POSTGRES_LEGACY_CREDENTIAL_FILE),
            "recovered-secret\n",
        )
        .unwrap();
        let manager = LocalCatalogDatabaseManager {
            runtime_paths: AppRuntimePaths {
                app_data_dir,
                log_dir: root.path().join("logs"),
                backup_dir: root.path().join("backups"),
            },
        };

        let state = manager.load_state().unwrap();
        assert_eq!(state.version, 4);
        assert_eq!(state.password, "recovered-secret");
        assert_eq!(state.port, 15432);
    }

    #[test]
    fn invalid_state_cannot_replace_existing_postgres_authority() {
        let root = tempfile::tempdir().unwrap();
        let app_data_dir = root.path().join("app-data");
        let service_root = app_data_dir.join(LOCAL_POSTGRES_DIR);
        let data_dir = service_root.join(LOCAL_POSTGRES_DATA_DIR);
        fs::create_dir_all(&data_dir).unwrap();
        fs::write(data_dir.join("PG_VERSION"), "14\n").unwrap();
        let invalid_state_path = service_root.join(LOCAL_POSTGRES_STATE_FILE);
        fs::write(&invalid_state_path, "{ invalid state").unwrap();
        let manager = LocalCatalogDatabaseManager {
            runtime_paths: AppRuntimePaths {
                app_data_dir,
                log_dir: root.path().join("logs"),
                backup_dir: root.path().join("backups"),
            },
        };

        let error = manager.load_or_create_state().unwrap_err();
        assert!(error
            .to_string()
            .contains("refusing to generate replacement authority"));
        assert_eq!(
            fs::read_to_string(invalid_state_path).unwrap(),
            "{ invalid state"
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

    #[test]
    fn restore_selection_uses_latest_normal_dump_and_excludes_safety_snapshots() {
        let directory = tempfile::tempdir().unwrap();
        let older = directory.path().join("catalog-manual-older.dump");
        fs::write(&older, b"older").unwrap();
        std::thread::sleep(Duration::from_millis(20));
        let safety = directory.path().join("pre-restore-newer.dump");
        fs::write(&safety, b"safety").unwrap();
        std::thread::sleep(Duration::from_millis(20));
        let latest = directory.path().join("pre-recipe-migration-latest.dump");
        fs::write(&latest, b"latest").unwrap();
        fs::write(directory.path().join("ignored.dump.partial"), b"partial").unwrap();

        assert_eq!(
            latest_restorable_backup(directory.path()).unwrap(),
            Some(latest)
        );
    }

    #[test]
    fn restore_selection_returns_none_when_only_safety_or_partial_files_exist() {
        let directory = tempfile::tempdir().unwrap();
        fs::write(directory.path().join("pre-restore-only.dump"), b"safety").unwrap();
        fs::write(directory.path().join("catalog.dump.partial"), b"partial").unwrap();

        assert_eq!(latest_restorable_backup(directory.path()).unwrap(), None);
    }

    #[tokio::test]
    #[allow(clippy::await_holding_lock)] // Serializes process-wide environment mutation through await.
    async fn automatic_restore_refuses_external_postgresql_before_touching_the_server() {
        let _lock = environment_lock();
        let previous_database_url = std::env::var_os("PHOTO_EDITOR_DATABASE_URL");
        std::env::set_var(
            "PHOTO_EDITOR_DATABASE_URL",
            "postgresql://administrator:secret@database.example/catalog",
        );
        let manager = LocalCatalogDatabaseManager {
            runtime_paths: AppRuntimePaths {
                app_data_dir: tempfile::tempdir().unwrap().path().join("app-data"),
                log_dir: tempfile::tempdir().unwrap().path().join("logs"),
                backup_dir: tempfile::tempdir().unwrap().path().join("backups"),
            },
        };

        let error = manager.restore_latest_backup().await.unwrap_err();
        match previous_database_url {
            Some(value) => std::env::set_var("PHOTO_EDITOR_DATABASE_URL", value),
            None => std::env::remove_var("PHOTO_EDITOR_DATABASE_URL"),
        }

        assert!(error.to_string().contains("external PostgreSQL server"));
    }

    #[cfg(unix)]
    #[test]
    fn interrupted_backup_removes_partial_archive_and_never_publishes_destination() {
        use std::os::unix::fs::PermissionsExt;

        let _lock = environment_lock();
        let root = tempfile::tempdir().unwrap();
        let bin_dir = root.path().join("bin");
        fs::create_dir_all(&bin_dir).unwrap();
        let pg_dump = bin_dir.join("pg_dump");
        fs::write(
            &pg_dump,
            "#!/bin/sh\nprevious=''\nfor argument in \"$@\"; do\n  if [ \"$previous\" = '--file' ]; then\n    printf 'incomplete archive' > \"$argument\"\n  fi\n  previous=\"$argument\"\ndone\necho 'simulated interrupted dump' >&2\nexit 23\n",
        )
        .unwrap();
        fs::set_permissions(&pg_dump, fs::Permissions::from_mode(0o700)).unwrap();
        let previous_bin_dir = std::env::var_os(POSTGRES_BIN_DIR_ENV);
        std::env::set_var(POSTGRES_BIN_DIR_ENV, &bin_dir);
        let manager = LocalCatalogDatabaseManager {
            runtime_paths: AppRuntimePaths {
                app_data_dir: root.path().join("app-data"),
                log_dir: root.path().join("logs"),
                backup_dir: root.path().join("backups"),
            },
        };
        let destination = root.path().join("backups/catalog.dump");

        let error = manager
            .write_backup_archive("postgresql://unused", &destination)
            .unwrap_err();
        match previous_bin_dir {
            Some(value) => std::env::set_var(POSTGRES_BIN_DIR_ENV, value),
            None => std::env::remove_var(POSTGRES_BIN_DIR_ENV),
        }

        assert!(error.to_string().contains("simulated interrupted dump"));
        assert!(!destination.exists());
        assert!(!destination.with_extension("dump.partial").exists());
    }

    #[cfg(unix)]
    #[test]
    fn completed_backup_is_owner_only_atomically_published_and_never_overwritten() {
        use std::os::unix::fs::PermissionsExt;

        let _lock = environment_lock();
        let root = tempfile::tempdir().unwrap();
        let bin_dir = root.path().join("bin");
        fs::create_dir_all(&bin_dir).unwrap();
        let pg_dump = bin_dir.join("pg_dump");
        fs::write(
            &pg_dump,
            "#!/bin/sh\nprevious=''\nfor argument in \"$@\"; do\n  if [ \"$previous\" = '--file' ]; then\n    printf 'complete custom archive' > \"$argument\"\n  fi\n  previous=\"$argument\"\ndone\n",
        )
        .unwrap();
        fs::set_permissions(&pg_dump, fs::Permissions::from_mode(0o700)).unwrap();
        let previous_bin_dir = std::env::var_os(POSTGRES_BIN_DIR_ENV);
        std::env::set_var(POSTGRES_BIN_DIR_ENV, &bin_dir);
        let manager = LocalCatalogDatabaseManager {
            runtime_paths: AppRuntimePaths {
                app_data_dir: root.path().join("app-data"),
                log_dir: root.path().join("logs"),
                backup_dir: root.path().join("backups"),
            },
        };
        let destination = root.path().join("backups/catalog.dump");

        manager
            .write_backup_archive("postgresql://unused", &destination)
            .unwrap();
        let original = fs::read(&destination).unwrap();
        let second_error = manager
            .write_backup_archive("postgresql://unused", &destination)
            .unwrap_err();
        match previous_bin_dir {
            Some(value) => std::env::set_var(POSTGRES_BIN_DIR_ENV, value),
            None => std::env::remove_var(POSTGRES_BIN_DIR_ENV),
        }

        assert_eq!(original, b"complete custom archive");
        assert_eq!(fs::read(&destination).unwrap(), original);
        assert_eq!(
            fs::metadata(&destination).unwrap().permissions().mode() & 0o777,
            0o600
        );
        assert_eq!(
            second_error.downcast_ref::<io::Error>().unwrap().kind(),
            io::ErrorKind::AlreadyExists
        );
        assert!(!destination.with_extension("dump.partial").exists());
    }

    #[cfg(unix)]
    #[test]
    fn corrupt_dump_fails_validation_without_running_a_restore() {
        use std::os::unix::fs::PermissionsExt;

        let _lock = environment_lock();
        let root = tempfile::tempdir().unwrap();
        let bin_dir = root.path().join("bin");
        fs::create_dir_all(&bin_dir).unwrap();
        let pg_restore = bin_dir.join("pg_restore");
        fs::write(
            &pg_restore,
            "#!/bin/sh\necho 'invalid custom archive' >&2\nexit 1\n",
        )
        .unwrap();
        fs::set_permissions(&pg_restore, fs::Permissions::from_mode(0o700)).unwrap();
        let corrupt_dump = root.path().join("corrupt.dump");
        fs::write(&corrupt_dump, b"not a PostgreSQL custom archive").unwrap();
        let previous_bin_dir = std::env::var_os(POSTGRES_BIN_DIR_ENV);
        std::env::set_var(POSTGRES_BIN_DIR_ENV, &bin_dir);
        let manager = LocalCatalogDatabaseManager {
            runtime_paths: AppRuntimePaths {
                app_data_dir: root.path().join("app-data"),
                log_dir: root.path().join("logs"),
                backup_dir: root.path().join("backups"),
            },
        };

        let error = manager.validate_backup(&corrupt_dump).unwrap_err();
        match previous_bin_dir {
            Some(value) => std::env::set_var(POSTGRES_BIN_DIR_ENV, value),
            None => std::env::remove_var(POSTGRES_BIN_DIR_ENV),
        }

        assert!(error
            .to_string()
            .contains("PostgreSQL backup validation failed"));
        assert!(error.to_string().contains("invalid custom archive"));
    }
}
