use std::env;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Mutex, OnceLock};

use opentelemetry::trace::TracerProvider as _;
use opentelemetry::KeyValue;
use opentelemetry_otlp::{SpanExporter, WithExportConfig};
use opentelemetry_sdk::trace::SdkTracerProvider;
use opentelemetry_sdk::Resource;
use tracing_subscriber::layer::SubscriberExt;
use tracing_subscriber::{EnvFilter, Registry};

const OTEL_EXPORTER_OTLP_ENDPOINT: &str = "OTEL_EXPORTER_OTLP_ENDPOINT";
const OTEL_EXPORTER_OTLP_TRACES_ENDPOINT: &str = "OTEL_EXPORTER_OTLP_TRACES_ENDPOINT";
const OTEL_SERVICE_NAMESPACE: &str = "OTEL_SERVICE_NAMESPACE";
const OTEL_DEPLOYMENT_ENVIRONMENT: &str = "OTEL_DEPLOYMENT_ENVIRONMENT";
static OTEL_INITIALIZED: AtomicBool = AtomicBool::new(false);
static TRACER_PROVIDER: OnceLock<Mutex<Option<SdkTracerProvider>>> = OnceLock::new();

pub fn init(service_name: &str, service_version: &str) {
    if OTEL_INITIALIZED.load(Ordering::SeqCst) {
        return;
    }

    let Some(endpoint) = exporter_endpoint() else {
        return;
    };

    let exporter = match SpanExporter::builder()
        .with_tonic()
        .with_endpoint(endpoint)
        .build()
    {
        Ok(exporter) => exporter,
        Err(error) => {
            eprintln!("photo-telemetry: failed to build OTLP exporter: {error}");
            return;
        }
    };

    let tracer_provider = SdkTracerProvider::builder()
        .with_simple_exporter(exporter)
        .with_resource(resource(service_name, service_version))
        .build();
    let tracer = tracer_provider.tracer(service_name.to_string());

    let env_filter = EnvFilter::try_from_default_env().unwrap_or_else(|_| EnvFilter::new("info"));
    let subscriber = Registry::default()
        .with(env_filter)
        .with(tracing_opentelemetry::layer().with_tracer(tracer));

    match tracing::subscriber::set_global_default(subscriber) {
        Ok(()) => {
            let _ = TRACER_PROVIDER.set(Mutex::new(Some(tracer_provider)));
            OTEL_INITIALIZED.store(true, Ordering::SeqCst);
        }
        Err(error) => {
            eprintln!("photo-telemetry: failed to install tracing subscriber: {error}");
        }
    }
}

pub fn shutdown() {
    if let Some(provider) = TRACER_PROVIDER.get() {
        let mut guard = provider.lock().expect("tracer provider mutex poisoned");
        if let Some(tracer_provider) = guard.take() {
            let _ = tracer_provider.shutdown();
            OTEL_INITIALIZED.store(false, Ordering::SeqCst);
        }
    }
}

fn exporter_endpoint() -> Option<String> {
    env::var(OTEL_EXPORTER_OTLP_TRACES_ENDPOINT)
        .ok()
        .filter(|value| !value.trim().is_empty())
        .or_else(|| {
            env::var(OTEL_EXPORTER_OTLP_ENDPOINT)
                .ok()
                .filter(|value| !value.trim().is_empty())
        })
}

fn resource(service_name: &str, service_version: &str) -> Resource {
    let mut attributes = vec![KeyValue::new(
        "service.version",
        service_version.to_string(),
    )];

    if let Ok(session_id) = env::var("PHOTO_EDITOR_SESSION_ID") {
        if !session_id.trim().is_empty() {
            attributes.push(KeyValue::new("service.instance.id", session_id));
        }
    }
    if let Ok(namespace) = env::var(OTEL_SERVICE_NAMESPACE) {
        if !namespace.trim().is_empty() {
            attributes.push(KeyValue::new("service.namespace", namespace));
        }
    }
    if let Ok(environment) = env::var(OTEL_DEPLOYMENT_ENVIRONMENT) {
        if !environment.trim().is_empty() {
            attributes.push(KeyValue::new("deployment.environment.name", environment));
        }
    }

    Resource::builder()
        .with_service_name(service_name.to_string())
        .with_attributes(attributes)
        .build()
}
