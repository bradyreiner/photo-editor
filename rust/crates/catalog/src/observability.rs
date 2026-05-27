use std::env;
use std::fs::{self, File, OpenOptions};
use std::io::Write;
use std::path::{Path, PathBuf};
use std::sync::Mutex;

use serde_json::{Map, Value};
use time::format_description::well_known::Rfc3339;
use time::OffsetDateTime;

use crate::AppRuntimePaths;

const SESSION_ID_ENV: &str = "PHOTO_EDITOR_SESSION_ID";
const ENABLED_ENV: &str = "PHOTO_EDITOR_ENABLE_DEV_OBSERVABILITY";
const LOG_LEVEL_ENV: &str = "PHOTO_EDITOR_LOG_LEVEL";
const RUN_DIRECTORY_ENV: &str = "PHOTO_EDITOR_OBSERVABILITY_RUN_DIR";

pub struct Logger {
    component: String,
    session_id: String,
    log_level: String,
    run_directory: PathBuf,
    file: Mutex<Option<File>>,
}

impl Logger {
    pub fn from_env(component: &str) -> std::io::Result<Self> {
        if env::var(ENABLED_ENV).unwrap_or_default() != "1" {
            return Ok(Self {
                component: component.to_string(),
                session_id: String::new(),
                log_level: String::new(),
                run_directory: PathBuf::new(),
                file: Mutex::new(None),
            });
        }

        photo_telemetry::init(component, env!("CARGO_PKG_VERSION"));

        let runtime_paths = AppRuntimePaths::from_env();
        runtime_paths.ensure_directories()?;

        let session_id = env::var(SESSION_ID_ENV)
            .unwrap_or_else(|_| format!("standalone-{}", std::process::id()));
        let log_level = env::var(LOG_LEVEL_ENV).unwrap_or_else(|_| "info".to_string());
        let run_directory = env::var_os(RUN_DIRECTORY_ENV)
            .map(PathBuf::from)
            .unwrap_or_else(|| default_run_directory(&runtime_paths.log_dir, &session_id));

        fs::create_dir_all(&run_directory)?;
        let file = OpenOptions::new()
            .create(true)
            .append(true)
            .open(run_directory.join(format!("{component}.jsonl")))?;

        Ok(Self {
            component: component.to_string(),
            session_id,
            log_level,
            run_directory,
            file: Mutex::new(Some(file)),
        })
    }

    pub fn info(&self, event: &str, fields: Value) {
        if self.enabled("info") {
            let _ = self.write_record("info", event, fields);
        }
    }

    pub fn warn(&self, event: &str, fields: Value) {
        if self.enabled("warning") {
            let _ = self.write_record("warning", event, fields);
        }
    }

    pub fn error(&self, event: &str, fields: Value) {
        let _ = self.write_record("error", event, fields);
    }

    pub fn run_directory(&self) -> &Path {
        &self.run_directory
    }

    fn enabled(&self, severity: &str) -> bool {
        if self.file.lock().expect("logger mutex poisoned").is_none() {
            return false;
        }
        match self.log_level.as_str() {
            "trace" | "debug" | "info" => true,
            "warning" => severity != "info",
            "error" => severity == "error",
            _ => true,
        }
    }

    fn write_record(&self, severity: &str, event: &str, fields: Value) -> std::io::Result<()> {
        let fields = standardized_fields(event, fields);
        let fields_json = serde_json::to_string(&fields).unwrap_or_else(|_| "null".to_string());
        let mut record = Map::new();
        record.insert(
            "timestamp".to_string(),
            Value::String(
                OffsetDateTime::now_utc()
                    .format(&Rfc3339)
                    .unwrap_or_else(|_| "1970-01-01T00:00:00Z".to_string()),
            ),
        );
        record.insert("severity".to_string(), Value::String(severity.to_string()));
        record.insert(
            "component".to_string(),
            Value::String(self.component.clone()),
        );
        record.insert(
            "session_id".to_string(),
            Value::String(self.session_id.clone()),
        );
        record.insert("pid".to_string(), Value::from(std::process::id()));
        record.insert("event".to_string(), Value::String(event.to_string()));
        match fields {
            Value::Object(object) => {
                record.insert("fields".to_string(), Value::Object(object));
            }
            other => {
                record.insert("fields".to_string(), other);
            }
        }

        let mut guard = self.file.lock().expect("logger mutex poisoned");
        if let Some(file) = guard.as_mut() {
            serde_json::to_writer(&mut *file, &Value::Object(record))?;
            file.write_all(b"\n")?;
            file.flush()?;
        }

        let span = tracing::info_span!(
            "catalog.observability",
            component = %self.component,
            session_id = %self.session_id,
            event = %event,
            severity = %severity
        );
        let _guard = span.enter();
        match severity {
            "warning" => tracing::warn!(fields = %fields_json, "catalog observability event"),
            "error" => tracing::error!(fields = %fields_json, "catalog observability event"),
            _ => tracing::info!(fields = %fields_json, "catalog observability event"),
        }
        Ok(())
    }
}

fn workflow_for_event(event: &str) -> Option<&'static str> {
    if event.starts_with("preview.") {
        Some("preview")
    } else if event.starts_with("thumbnail.") || event.starts_with("benchmark.thumbnail") {
        Some("thumbnail")
    } else if event.starts_with("catalog.") || event.starts_with("catalog_") {
        Some("catalog")
    } else if event.starts_with("job.") {
        Some("jobs")
    } else if event.starts_with("desktop.startup") {
        Some("startup")
    } else if event.starts_with("benchmark.") {
        Some("benchmark")
    } else {
        None
    }
}

fn insert_if_missing(object: &mut Map<String, Value>, key: &str, value: Value) {
    if object.contains_key(key) || value.is_null() {
        return;
    }
    if value.as_str().is_some_and(|value| value.is_empty()) {
        return;
    }
    object.insert(key.to_string(), value);
}

fn standardized_fields(event: &str, fields: Value) -> Value {
    let mut object = match fields {
        Value::Object(object) => object,
        other => {
            let mut object = Map::new();
            object.insert("value".to_string(), other);
            object
        }
    };

    if let Some(workflow) = workflow_for_event(event) {
        insert_if_missing(&mut object, "workflow", Value::String(workflow.to_string()));
    }
    insert_if_missing(
        &mut object,
        "thread",
        Value::String(format!("{:?}", std::thread::current().id())),
    );
    if let Some(reason) = object.get("render_reason").cloned() {
        insert_if_missing(&mut object, "reason", reason);
    }
    if !object.contains_key("interactive") {
        if let Some(full_resolution) = object.get("full_resolution").and_then(Value::as_bool) {
            object.insert("interactive".to_string(), Value::Bool(!full_resolution));
        } else if let Some(draft_interactive) = object.get("draft_interactive").cloned() {
            insert_if_missing(&mut object, "interactive", draft_interactive);
        }
    }
    if let Some(cache_hit) = object.get("decoded_cache_hit").cloned() {
        insert_if_missing(&mut object, "cache_hit", cache_hit);
    }
    if let Some(cache_hit) = object.get("linear_cache_hit").cloned() {
        insert_if_missing(&mut object, "cache_hit", cache_hit);
    }
    if let Some(cache_hit) = object.get("decoded_cache_lookup").cloned() {
        insert_if_missing(&mut object, "cache_hit", cache_hit);
    }

    Value::Object(object)
}

fn default_run_directory(log_dir: &Path, session_id: &str) -> PathBuf {
    let timestamp = OffsetDateTime::now_utc()
        .format(&Rfc3339)
        .unwrap_or_else(|_| "1970-01-01T00:00:00Z".to_string())
        .replace([':', '.'], "");
    log_dir
        .join("runs")
        .join(format!("{timestamp}-{session_id}"))
}

#[cfg(test)]
mod tests {
    use super::standardized_fields;
    use serde_json::json;

    #[test]
    fn standardized_fields_adds_perf_aliases() {
        let fields = standardized_fields(
            "preview.render.completed",
            json!({
                "render_reason": "slider_drag",
                "full_resolution": false,
                "decoded_cache_hit": true
            }),
        );

        assert_eq!(fields["workflow"], "preview");
        assert_eq!(fields["reason"], "slider_drag");
        assert_eq!(fields["interactive"], true);
        assert_eq!(fields["cache_hit"], true);
        assert!(fields["thread"]
            .as_str()
            .is_some_and(|value| !value.is_empty()));
    }
}
