# Release readiness remediation — 2026-11-18

This records local remediation evidence for the [production launch checklist](../production-launch-checklist.md).
Hosted delivery, installed-app acceptance and platform qualification remain separate checks.
The earlier [customer-system implementation record](customer-platform-2026-11-17/README.md)
contains the prior Account-panel fixtures and workflow details.

## Customer backend and dependency verification

The patched customer service completed a clean `npm ci --legacy-peer-deps` under
Node 24. Its final `npm run check` with `LOOKSY_RUN_D1_RECOVERY_TEST=1` passed:

- TypeScript typecheck.
- 42 setup checks, including an actual isolated D1 backup/restore; the separate
  opt-in staff SQL integration case was excluded from this run.
- 71 Worker/D1 integration cases across five files, using synthetic email and
  billing responses, temporary databases and ephemeral signing keys.
- Wrangler deployment dry-run: 135.26 KiB, 30.71 KiB compressed. No deployment occurred.

The backup rehearsal preserved all 18 customer-service tables and verified their
row fingerprints, schema, indices and triggers. It retained account sessions,
consumed challenges, revoked devices/VIP grants and Paddle purchase mappings.
Restore creates every table before loading rows, avoiding a D1 export ordering
failure when installations reference the later accounts table. These hosted
operations tables are separate from desktop photo catalogs.

The development dependency graph was patched without changing the application
API or downgrading the Cloudflare test pool. The selected compatible versions are
Vitest 4.1.12, Wrangler 4.149.0, Miniflare 5.20261006.1-alpha and matching workerd,
sharp 0.35.5, undici 7.30.1, Zod 4.6.6, Vite 8.3.3 and source-map-js 1.2.2.
Generated Worker types match that runtime; the existing compatibility date remains.

The official npm audit initially reported 13 affected packages (12 high, one
moderate). The patched full audit still reports six high package findings, all
derived from [GHSA-fc5q-39xc-vjpf](https://github.com/advisories/GHSA-fc5q-39xc-vjpf),
which GitHub withdrew on November 18. npm still returns the withdrawn advisory for
picomatch 4.0.7 and its dependency chain; no patched 4.x release was published at
this check. No audit finding was suppressed or allowlisted. `npm audit --omit=dev`
reports zero findings for the deployed Worker dependency set. Patch references and
remaining setup fields are in the [service runbook](../../services/customer-platform/README.md).

With Node 24 selected, reproduce the service checks from `services/customer-platform`:

```sh
npm ci --legacy-peer-deps
WRANGLER_LOG_PATH=/tmp/looksy-customer-check.log LOOKSY_RUN_D1_RECOVERY_TEST=1 npm run check
npm audit --omit=dev --registry=https://registry.npmjs.org
npm audit --registry=https://registry.npmjs.org
npx wrangler deploy --dry-run --outdir /tmp/looksy-worker
```

The full audit command intentionally retains its nonzero result while npm returns
the withdrawn advisory. The configured npm mirror does not implement the audit
endpoint, so audit verification used the official registry explicitly.

## Private staging without a purchased domain

The service now supports an explicit `routingMode: "workers-dev"` staging input.
The generated API issuer must exactly match the configured worker name and account
subdomain, preview URLs remain disabled, and production setup rejects this route.
Staff JWT verification and enrolled support/admin roles remain required.

Resend's `onboarding@resend.dev` sender is allowed only for private staging with an
explicit `emailTestRecipient`. The Worker rejects other recipients before sending;
live mode rejects that sender. The configured recipient must be the email registered
with Resend. These guards are covered by setup and Worker tests; no actual email
was sent. See the [no-domain staging walkthrough](../customer-platform-setup.md#private-staging-without-a-domain)
and [Resend's testing restriction](https://resend.com/docs/knowledge-base/403-error-resend-dev-domain).
Paddle [sandbox checkout URL settings](https://developer.paddle.com/api-reference/settings/update-checkout-url/)
accept any HTTPS URL; live accounts require an approved domain.

The root session validated a Cloudflare account with read-only account/user OAuth
access and read its dashboard through native accessibility. It identified the
account subdomain `brady-reiner.workers.dev` and existing Pages project host
`photo-editor-48a.pages.dev`. The proposed customer API is
`https://looksy-customers-staging.brady-reiner.workers.dev`; no customer Worker or D1
resource was created. A mode-0600, git-ignored local handoff configuration at
`services/customer-platform/wrangler.staging-handoff.local.jsonc` selects that
account and proposed issuer. Unverified D1, Access, sender-recipient and merchant
values remain explicit placeholders; it is not deployment-ready configuration.

The user completed Zero Trust Free onboarding. The root session then read the
Zero Trust overview: team `noisy-base-e9d1`, plan **Zero Trust Free**. The ignored
handoff now uses `noisy-base-e9d1.cloudflareaccess.com`. A narrow `/admin*` Access
application, its audience, MFA policy and enrolled owner subject are still pending;
no admin application was created by the agent. Customer `/v1/*` paths and billing
notifications must remain reachable with their own authentication. Actual edge
policy coverage and `/admin*` behavior remain unverified.

## Hosted checkout routing remediation

A bounded public GET found a real redirect loop on the existing Pages deployment:
`/checkout.html` returned 308 to `/checkout`, while `/checkout` returned 308 to
itself. The site contained a 200 rewrite from the clean path back to its `.html`
file. [Pages already serves matching extensionless HTML routes and redirects `.html` requests to those clean paths](https://developers.cloudflare.com/pages/configuration/serving-pages/#route-matching).

The source fix removes all six redundant checkout/legal HTML rewrites and retains
Pages' native routing. Eleven checkout Node tests pass, including a regression
check covering checkout, privacy, terms, refunds, support and licenses. The public
checkout remains blocked until a new Pages deployment publishes this fix and a
fresh GET confirms real checkout HTML without a loop, preserving `?_ptxn=`.
The known Pages origin in the ignored handoff is a candidate, not proof of a
working merchant checkout. Its public Paddle client configuration also remains
an operator setup requirement. A local Pages emulator served the customer-service
asset directory instead of the site; that diagnostic is excluded from acceptance.

## Cloudflare authorization boundary

The current account/user-only OAuth login can validate identity but the Workers
subdomain GET returned 403. The [endpoint](https://developers.cloudflare.com/api/resources/workers/subresources/subdomains/methods/get/)
accepts **Workers Scripts Read** or **Workers Scripts Write**. Pure discovery can
use an account-scoped API token with Workers Scripts Read, or the operator can
copy the subdomain from the dashboard without changing authorization.

`wrangler login --scopes-list` in the pinned CLI exposes `account:read`, `user:read`
and `workers_scripts:write`; it does not expose `workers:read` as a Wrangler login
scope. Consequently, its minimal supported OAuth addition for this endpoint is
`workers_scripts:write`, which also permits changes and must not be described as
read-only. No OAuth scopes or credentials were changed during this remediation.
[Wrangler scope selection documentation](https://developers.cloudflare.com/workers/wrangler/commands/general/#login).

Access application/policy inspection needs [Access: Apps and Policies Read](https://developers.cloudflare.com/api/resources/zero_trust/subresources/access/subresources/applications/methods/get/).
Creating policies needs [Access: Apps and Policies Write](https://developers.cloudflare.com/api/resources/zero_trust/subresources/access/subresources/applications/subresources/policies/methods/create/),
independent of Workers deployment authorization. Dashboard setup requires an
appropriate Access-edit/Zero Trust account role; [the roles reference](https://developers.cloudflare.com/cloudflare-one/roles-permissions/)
distinguishes those from read-only access. D1 creation/migration, Worker deployment
and secrets upload require their own write permissions. No broad login, resource
creation, deployment, staff enrollment, email delivery or payment was performed.

## Native builds, source closure and release guards

The local macOS Release application rebuilt successfully with the updated Rust
release helpers/FFI. The headless preset, with the Mac-specific Metal preview
backend disabled, completed all 92 registered cases: 91 passed and the existing
optional depth-motion worker case was skipped (42.14 seconds). This is local
correctness evidence, not a clean-machine installer or physical display proof.

The focused desktop checks cover signed customer access/expiry, new account
request headers, phase tool policy and project memory controls, layer-effect
history/admission, style editing, presets, timeline grades/filters/frame scheduling
and the selected Little CMS backend. Nine cases passed initially; the preset and
frame-scheduling fixtures then passed three consecutive runs after correcting
stale independent-selection setup and cross-thread event-order assumptions.
Neither correction changed production preset or playback behavior.

The broader timeline controller check remains failing. A stale audio assertion
was corrected to require zero gain for placeholder video without a known audio
stream and to prove gain editing independently on a declared audio clip. That
section now passes. With a real bundled PostgreSQL runtime and isolated writable
test directories, the run then reaches an actual Fit-to-Fill export assertion
failure; duration/pixel diagnosis remains open. Program-scope and generated-source
workflows passed. Do not infer that all desktop regressions pass from the focused
results above.

Windows/Linux remediation includes explicit standard-library headers, portable
move-only edit scopes and designated initializer order, UTF-8/path handling in
Python checks, explicit Bash for shell-script tests, a pinned Windows media
decoder, and actual owner-only Windows ACL creation/inspection for private evidence
files. The ICC oracle now links the same validated backend as the engine.
Actual Windows execution of those repairs remains a fresh-CI requirement.

The duplication gate passed unchanged limits: **725 total / 504 production**
clones against limits **732 / 505**. Permission admission continues to be enforced;
shared helpers do not grant new access. The staged tracked-only source export
inventoried 4,075 files and passed source closure. Two absolute local `node_modules`
symlinks were removed from tracking, while their local installations were retained.
New cache/dependency paths are ignored.

## Rust and secret-scanner remediation

SQLx and compatible transitive dependencies were updated, with dynamic SQL using
bound QueryBuilder inputs. `cargo audit --deny warnings` passed without an ignored
advisory. Workspace tests passed 78 cases with five existing ignored cases; Clippy
with warnings denied passed. Three real PostgreSQL regression cases and the
release-helper authority/rollback smoke ran against disposable databases and
passed. The release workspace and FFI rebuilt successfully; the active user
catalog was not used for these tests.

The secret workflow now installs a pinned, SHA-256-verified open-source Gitleaks
CLI and scans full history. Reviewed exceptions require matching paths, detector
rules and known non-secret fields/fixture values. Guard fixtures prove new
credentials in the same paths still fail. The full-history scan passed across
72 commits (2.97 GB). No real credential or history rewrite was required.

## Remote and release qualification

The pushed implementation revision is `55932696`; its customer-platform workflow
passes. Its native and security runs exposed the failures repaired in this batch,
so those runs do not qualify this remediation revision. The next push must obtain
new Windows, Linux, sanitizer, Mac, Rust and security results.

No installer, public release tag, new hosted customer service, payment or real
email was published by these checks. Apple membership/Developer ID/notarization,
Windows signing, isolated hosted accounts/admin authorization, sandbox purchases,
crash delivery/symbols and two-version installed updates remain acceptance gates.
