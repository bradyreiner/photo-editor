# Local Automation Security

Framehouse automation is a privileged local interface. Loopback binding limits network reachability,
but it is not an authentication boundary: browsers, sandboxed applications with localhost access,
and other processes running as the signed-in user may all be able to connect.

## Default posture

- The MCP HTTP listener is off unless `--enable-automation` or the
  `PHOTO_EDITOR_ENABLE_AUTOMATION` launch policy explicitly enables it.
- A new enabled session receives only the `read` capability unless
  `PHOTO_EDITOR_AUTOMATION_CAPABILITIES` explicitly lists additional comma-separated capabilities.
- Valid capabilities are `read`, `draft`, `export`, and `commit`; unknown values fail startup.
- The server binds only to loopback, requires an exact localhost `Host`, rejects non-local and opaque
  browser `Origin` values, and requires a bearer capability on every accepted GET and POST.
- The 256-bit bearer value is generated for each start and is never placed in endpoint URLs, command
  arguments, logs, telemetry, or error messages.
- The bearer value is stored only in the owner-readable automation session file. The runtime directory,
  session file, and local socket are created with owner-only permissions where the platform exposes them.
- The server limits header and body sizes, JSON nesting, open connections, requests per minute, and
  concurrent tool execution. Tool schemas are filtered to the session capability before discovery.

## Trust boundaries

The app owner and the interactive Framehouse process are trusted. The following are untrusted even when
they run on the same machine:

- websites and browser extensions, including DNS-rebinding and cross-origin requests;
- sandboxed creative applications or plug-ins with localhost access;
- other user processes, terminal sessions, background agents, and remote-control helpers;
- malformed or adversarial model output; and
- stale session files, copied credentials, and replayed requests.

Local-only transport, `Origin`, and `Host` checks are defense in depth. Possession of the current bearer
capability is the authentication decision. Model-proposed tool calls are additionally constrained by the
run-phase allowlist and server-side typed argument validation.

## Sensitive operations

Draft mutation, export, and commit are distinct capabilities. A read-only caller cannot discover or call
those tools. Capability alone does not replace interactive approval: export, commit, destructive repair,
and equivalent irreversible operations must also present a narrow app-issued, expiring, one-shot approval
receipt bound to the session, project, asset set, operation, and arguments. Until that receipt flow is
complete, release policy must not grant `export` or `commit` to external automation sessions.

## Review requirements

Any new transport must receive a threat-model update and focused endpoint security tests. Release review
must cover missing, invalid, stale, and replayed credentials; capability escalation; Host and Origin
variants; request smuggling and resource exhaustion; secret redaction; and attempted access from a second
local process. Security decisions should be audited without recording bearer values or private image data.
