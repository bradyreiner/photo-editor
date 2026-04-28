use std::io::{self, BufRead, Write};

use photo_editor_mcp::PhotoEditorMcpServer;

fn main() {
    photo_telemetry::init("photo-editor-mcp", env!("CARGO_PKG_VERSION"));

    let server = match PhotoEditorMcpServer::discover() {
        Ok(server) => server,
        Err(message) => {
            eprintln!("photo-editor-mcp: {message}");
            std::process::exit(1);
        }
    };

    let stdin = io::stdin();
    let stdout = io::stdout();
    let mut output = stdout.lock();

    for line in stdin.lock().lines() {
        let line = match line {
            Ok(line) => line,
            Err(error) => {
                eprintln!("photo-editor-mcp: failed to read stdin: {error}");
                break;
            }
        };

        let responses = server.handle_input_line(&line);
        if responses.is_empty() {
            continue;
        }

        let response_value = if responses.len() == 1 {
            responses.into_iter().next().expect("single response")
        } else {
            serde_json::Value::Array(responses)
        };

        let encoded = match serde_json::to_string(&response_value) {
            Ok(encoded) => encoded,
            Err(error) => {
                eprintln!("photo-editor-mcp: failed to encode response: {error}");
                continue;
            }
        };

        if writeln!(output, "{encoded}").is_err() {
            eprintln!("photo-editor-mcp: failed to write response");
            break;
        }
        if output.flush().is_err() {
            eprintln!("photo-editor-mcp: failed to flush response");
            break;
        }
    }

    photo_telemetry::shutdown();
}
