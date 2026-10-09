//! Bounded stdin transport for large edit recipes. Keep legacy argv callers
//! working, but never put masks, pixels or approval evidence in process listings.
use std::io::Read;

use serde::Deserialize;

pub const MAX_HELPER_PAYLOAD_BYTES: u64 = 256 * 1024 * 1024;
const SCHEMA: &str = "photo-editor-catalog-arguments:v1";

#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
struct Envelope {
    schema: String,
    arguments: Vec<String>,
}

pub fn read_helper_arguments() -> Result<Vec<String>, String> {
    decode_helper_arguments(std::env::args().collect(), std::io::stdin().lock())
}

fn decode_helper_arguments(
    arguments: Vec<String>,
    input: impl Read,
) -> Result<Vec<String>, String> {
    decode_with_limit(arguments, input, MAX_HELPER_PAYLOAD_BYTES)
}

fn decode_with_limit(
    arguments: Vec<String>,
    input: impl Read,
    limit: u64,
) -> Result<Vec<String>, String> {
    if arguments.get(1).map(String::as_str) != Some("--stdin-json-v1") {
        return Ok(arguments);
    }
    if arguments.len() != 2 {
        return Err("stdin recipe transport does not accept extra command-line arguments".into());
    }
    let mut bytes = Vec::new();
    input
        .take(limit + 1)
        .read_to_end(&mut bytes)
        .map_err(|_| "could not read recipe payload from stdin")?;
    if bytes.len() as u64 > limit {
        return Err("recipe stdin payload exceeds the 256 MiB safety limit".into());
    }
    let envelope: Envelope = serde_json::from_slice(&bytes)
        .map_err(|_| "recipe stdin payload is not a valid argument envelope")?;
    if envelope.schema != SCHEMA || envelope.arguments.len() > 32 {
        return Err("unsupported recipe stdin schema or argument count".into());
    }
    let mut decoded = vec![arguments[0].clone()];
    decoded.extend(envelope.arguments);
    Ok(decoded)
}

#[cfg(test)]
mod tests {
    use super::*;
    fn stdin_args() -> Vec<String> {
        vec!["save_edit_recipe".into(), "--stdin-json-v1".into()]
    }

    #[test]
    fn input_is_bounded_before_json_parsing() {
        assert!(decode_with_limit(stdin_args(), std::io::repeat(b' '), 1024)
            .unwrap_err()
            .contains("safety limit"));
        let bytes =
            serde_json::to_vec(&serde_json::json!({"schema": SCHEMA, "arguments": []})).unwrap();
        assert!(decode_with_limit(stdin_args(), bytes.as_slice(), bytes.len() as u64).is_ok());
        assert!(decode_with_limit(stdin_args(), bytes.as_slice(), bytes.len() as u64 - 1).is_err());
    }

    #[test]
    fn large_unicode_recipe_roundtrips_without_argv_limits() {
        let recipe = "mask-α\"\\\n".repeat(200_000);
        let bytes = serde_json::to_vec(&serde_json::json!({
            "schema": SCHEMA, "arguments": ["asset", recipe]
        }))
        .unwrap();
        let actual = decode_helper_arguments(stdin_args(), bytes.as_slice()).unwrap();
        assert_eq!(actual, vec!["save_edit_recipe", "asset", &recipe]);
    }

    #[test]
    fn large_timeline_upsert_payload_roundtrips_exact_argv_without_catalog_io() {
        use crate::PersistedTimelineDocument;

        let expected = PersistedTimelineDocument {
            timeline_id: "timeline-large-payload".into(),
            asset_id: "timeline:timeline-large-payload".into(),
            name: "Large timeline".into(),
            document_path: "/tmp/timeline.json".into(),
            render_path: "/tmp/timeline.mp4".into(),
            poster_path: "/tmp/timeline.jpg".into(),
            duration_seconds: 12.5,
            document_json: Some(serde_json::json!({
                "opaqueClipMetadata": format!("quote-\"-slash-\\-α-{}", "payload".repeat(8_192)),
                "clips": [{"clipId": "clip-1", "source": "media:still"}],
            })),
            created_at: "2026-10-06T00:00:00Z".into(),
            updated_at: "2026-10-06T00:00:01Z".into(),
        };
        let payload = serde_json::to_string(&expected).unwrap();
        assert!(payload.len() > 32 * 1024);
        let original_arguments = vec![
            "catalog_timeline_documents".to_owned(),
            "upsert".to_owned(),
            payload,
        ];
        let envelope = serde_json::to_vec(&serde_json::json!({
            "schema": SCHEMA,
            "arguments": &original_arguments[1..],
        }))
        .unwrap();
        let decoded_arguments = decode_helper_arguments(
            vec![
                "catalog_timeline_documents".into(),
                "--stdin-json-v1".into(),
            ],
            envelope.as_slice(),
        )
        .unwrap();

        assert_eq!(decoded_arguments, original_arguments);
        let decoded_document: PersistedTimelineDocument =
            serde_json::from_str(&decoded_arguments[2]).unwrap();
        assert_eq!(decoded_document, expected);
    }

    #[test]
    fn malformed_or_wrong_version_payloads_fail_closed() {
        let too_many = serde_json::to_vec(&serde_json::json!({
            "schema": SCHEMA, "arguments": vec!["argument"; 33]
        }))
        .unwrap();
        assert!(decode_helper_arguments(stdin_args(), too_many.as_slice()).is_err());
        for payload in [
            "",
            "{}",
            "[]",
            "{",
            r#"{"schema":"v2","arguments":[]}"#,
            r#"{"schema":"photo-editor-catalog-arguments:v1","arguments":[3]}"#,
            r#"{"schema":"photo-editor-catalog-arguments:v1","arguments":[],"extra":true}"#,
        ] {
            assert!(decode_helper_arguments(stdin_args(), payload.as_bytes()).is_err());
        }
        let mut args = stdin_args();
        args.push("unexpected".into());
        assert!(decode_helper_arguments(args, &b"{}"[..]).is_err());
    }

    #[test]
    fn legacy_arguments_do_not_read_stdin() {
        struct Unreadable;
        impl Read for Unreadable {
            fn read(&mut self, _: &mut [u8]) -> std::io::Result<usize> {
                panic!("legacy CLI read stdin")
            }
        }
        let args = vec!["save_edit_recipe".into(), "asset".into(), "{}".into()];
        assert_eq!(
            decode_helper_arguments(args.clone(), Unreadable).unwrap(),
            args
        );
    }
}
