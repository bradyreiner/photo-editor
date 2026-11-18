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
account, proposed issuer and the known Access team domain. D1, the Access app
audience/MFA/owner identity, sender-recipient and merchant values remain unresolved;
it is not deployment-ready configuration.

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
check covering checkout, privacy, terms, refunds, support and licenses. After the
existing Git-integrated Pages project automatically deployed the pushed fix, the
root session verified a public GET of
[`/checkout.html`](https://photo-editor-48a.pages.dev/checkout.html) reached HTTP 200
at `/checkout`, with the real Checkout — Looksy title and both `paddle-config.js`
and `checkout-page.js` script references. Public checkout routing is now verified.

The page's Paddle client configuration, provider products and authenticated
transaction flow remain unconfigured. This HTTP proof does not establish a paid
or sandbox purchase, webhook fulfillment, portal access or email delivery. A
local Pages emulator served the customer-service asset directory instead of the
site; that earlier diagnostic is excluded from acceptance.

## Free-only private staging constraint

The user requires staging to stay within the free allowances. The root session
verified the account's Workers plans page: **Free, $0, Current plan**; the Paid
$5/month-plus-usage upgrade was not selected. Zero Trust overview also shows
**Zero Trust Free**, team `noisy-base-e9d1`. No plan, subscription, payment method,
paid add-on or billing setting was changed by the agent. Usage counters and
other account subscriptions still need review; these confirmations do not certify
that every product in the account has no charges.

Published Cloudflare allowances checked on November 18:

| Product | Free allowance | Boundary relevant to this test |
| --- | --- | --- |
| [Workers](https://developers.cloudflare.com/workers/platform/limits/#daily-requests) | 100,000 requests/day, resetting at 00:00 UTC; 10 ms CPU/invocation | Daily request exhaustion returns error 1027; CPU exhaustion stops the invocation. The quota is shared by the account, including any Pages Functions. Keep the account on Free. |
| [D1](https://developers.cloudflare.com/d1/platform/pricing/#what-happens-if-i-exceed-the-daily-limits-on-reads-and-writes-or-the-total-storage-limit-on-the-free-plan) | 5 million rows read/day, 100,000 rows written/day, 5 GB total storage | Daily exhaustion returns query errors; full storage blocks new writes/schema changes. [Free limits](https://developers.cloudflare.com/d1/platform/limits/) also cap each database at 500 MB and the account at 10 databases. |
| [Pages](https://developers.cloudflare.com/pages/platform/limits/) | 500 builds/month; one concurrent build; 20-minute build timeout | Keep the existing project and limit unnecessary/preview builds. [Pure static requests are free and unlimited](https://developers.cloudflare.com/pages/functions/pricing/#static-asset-requests); adding Functions consumes Workers quota. |
| [Zero Trust Free](https://developers.cloudflare.com/reference-architecture/architectures/sase/) | Up to 50 users | Allow only the owner to the staff application initially. [An authenticated user consumes one seat; further users are blocked when seats are exhausted](https://developers.cloudflare.com/cloudflare-one/team-and-resources/users/seat-management/). Looksy customer email sign-in does not need an Access seat. |
| [Workers Logs through November 30](https://developers.cloudflare.com/workers/observability/logs/workers-logs/#pricing) | 200,000 log events/day; three-day retention | The private handoff keeps logs enabled with 1% head sampling. Sampling reduces volume; it is not a billing cap or a promise that every error is persisted. |
| [Observability from December 1](https://developers.cloudflare.com/observability/pricing/) | Free: 0.5 GB ingested/day; seven-day retention | Free ingestion stops at its daily allowance and resumes at 00:00 UTC. Extra ingestion is unavailable on Free. Paid continues ingesting with charges. Recheck the transition before extending the test. |

The ignored handoff now retains `observability.enabled=true` with
`head_sampling_rate=0.01`, and sets `triggers.crons=[]` for the initial manual
owner-only test. No production default was changed. With automatic cron paused,
billing reconciliation, queued webhook retries and expiry cleanup do not run on
a schedule; this configuration cannot qualify those operations. Webhooks and
explicit customer refresh still run on request. Enable a reviewed bounded schedule
only when testing scheduled recovery/retention, while still keeping the account
on Free. Existing application rate limits reduce email/API abuse after a request
arrives; they do not prevent all Worker invocations or guarantee account-wide
quota availability.

Before any deployment, confirm Workers Free and Zero Trust Free are still selected,
review existing usage, keep the Access allow policy restricted to the owner's
trusted identity, and resolve the remaining audience/MFA/identity and D1 fields.
Use one customer Worker and one dedicated staging D1 database, keep the checkout
site static, preserve `sandbox` billing, and avoid paid bindings, Logpush or other
paid products. No paid upgrade is authorized to repair an exhausted quota or a
Free-plan CPU failure; allow the test to stop and inspect usage instead. Pages
build watch paths and preview-branch settings can reduce builds, but no hosted
setting was changed in this audit.

[Spending limits](https://developers.cloudflare.com/workers/platform/spend-management/)
and alerts are distinct: alerts notify rather than block. Cloudflare's Workers
spending limit can reject invocations, but explicitly has reporting/enforcement
lag, excludes subscription fees and does not cover other products' charges. It is
not an exact maximum bill and is not a substitute for remaining on Free. Neither
source configuration nor a sampled log setting can certify a universal zero-cost
account; external email, payment, crash-reporting and signing providers have
separate plans that this Cloudflare audit did not verify. No email, payment or
remote workload was generated while preparing these controls.

## Remaining provider-account setup

The user confirmed that neither a Paddle nor a Resend account exists yet. The
next account steps are [Paddle sandbox signup](https://sandbox-login.paddle.com/signup)
and [Resend signup](https://resend.com/signup), completed by the user with their
own identity, business details and terms acceptance. The agent has not accepted
terms, created either account, selected a paid plan or entered payment details.
Use Paddle sandbox only. Select Resend's free option and keep transactional-overage
opt-in off; do not enable paid overages to complete testing. After signup, obtain
the sandbox product/price IDs, public checkout token, notification secret and
sending API key through the private operator setup. Keep secret values out of
source and chat. The owner's registered Resend email is still needed for the
strict test-recipient field. No real payment or email has been generated.

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
read-only. The user completed the requested fresh account/user read-only login;
no broader Worker deployment or Access scopes were granted.
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

The broader timeline controller check exposed stale audio and asynchronous
readiness assumptions. Placeholder video without a known audio stream must retain
zero gain; a declared audio clip independently proves gain editing. The Fit-to-Fill
test now awaits the controller's revision-specific output path after job completion.
An exact-object A/B comparison with the pre-refactor timeline implementation
produced identical real exports: duration 0.600 seconds and RGB 249/47/2, meeting
the unchanged strict thresholds. Channel-mapping save/reopen now follows the UI's
asynchronous save path and awaits completion rather than requiring an idle
synchronous checkpoint immediately after redo. One shared bounded job wait now
requires the exact completed output to exist and be published by the controller;
repeated Publish must produce a new path, using the original deadlines.

The final broad run passed those checks, professional two-pass delivery with
isolated WAV stems/PNG frames and saved mixer reopen, then **failed the strict
title-rendering pixel assertion** after 122.58 seconds. That actual pixel gate
remains unresolved and was not weakened. Program-scope and generated-source
workflows passed. All owned temporary PostgreSQL servers were stopped; user
catalogs were untouched. Do not infer that all desktop regressions pass from the
focused results above.

Windows/Linux remediation includes explicit standard-library headers, portable
move-only edit scopes and designated initializer order, UTF-8/path handling in
Python checks, explicit Bash for shell-script tests, a pinned Windows media
decoder, and actual owner-only Windows ACL creation/inspection for private evidence
files. The ICC oracle now links the same validated backend as the engine.
Actual Windows execution of those repairs remains a fresh-CI requirement.

The duplication gate passed unchanged limits: **725 total / 504 production**
clones against limits **732 / 505**. Permission admission continues to be enforced;
shared helpers do not grant new access. The final staged tracked-only source export
inventoried 4,076 files and passed source closure. Two absolute local `node_modules`
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

## Follow-up CI findings and repairs

The `a81e96f9` customer-platform workflow and Rust dependency audit pass. Windows
headless compilation and six formerly failing checks pass; only the Mac-signing
shell fixture remains failing on Git Bash. Linux headless compilation passes and
only the media diagnosis test fails for missing FFmpeg. The follow-up source adds
FFmpeg to the Linux test dependencies and makes the signing mock LF-only Bash
functions with canonical shell paths, preserving all signing-order assertions.

Sanitizer compilation exposed Clang's 256-level fold-expression limit while
inferring the 471-entry CIE observer table. Its type and length are now explicit;
all samples remain unchanged and three DCP/profile checks pass. A Rust 1.100
Clippy sort idiom and the architecture guard's old inline location for exclusive
private-file creation are also corrected; local Clippy and architecture checks
pass without relaxed checks.

The scanner's three new CI findings were public cache-prefix/profile literals in
the scanner test source itself. An exact script/rule/literal exception handles
those values; the guard now scans its actual current source and still detects
synthetic credentials in all seven exception paths. A CI-equivalent scan of remote
refs/tags and HEAD passes: 71 commits, 191.08 MB, zero findings. The larger earlier
local scan included a local backup branch, explaining its different byte count.
New CI must still verify the coordinated follow-up revision.

## Supported Mac runners and catalog startup

`530f5f71` passes the remote security and customer-platform workflows, and
repository hygiene. Its Rust formatting, Clippy and unit tests pass; the managed
PostgreSQL runtime proof then fails at server startup. The cancelled Mac jobs
use `macos-14`, which GitHub [retired on November 2](https://github.com/actions/runner-images/blob/main/README.md#available-images).
The follow-up changes both Mac CI jobs, the generated Apple-silicon release matrix
and the appcast job to the supported standard `macos-15` Apple-silicon image.
No larger-runner label or plan upgrade is used. Workflow lint and all 17 commerce
configuration tests pass; generated platform selection preserves Apple-silicon
and Windows x64 artifacts.

Managed PostgreSQL previously inherited the distribution's Unix socket directory.
A controlled same-catalog experiment reproduced startup failure with an unwritable
socket directory. The fixed helper explicitly disables Unix sockets and starts
that catalog on authenticated loopback TCP. It completes migrations, reports
`unix_socket_directories` empty and `listen_addresses=127.0.0.1`, and rejects an
incorrect password. The complete managed/external runtime proof passes all 12
steps, including credential recovery, pre-migration rollback, backup/restore,
corrupt/interrupted/failed-restore guards, relink, library move and external
catalog authority. Catalog unit tests pass 67 cases with five existing ignored;
workspace Clippy passes with warnings denied. Ubuntu's actual packaged runtime
still requires fresh CI confirmation. Failure cleanup now prints a bounded,
credential-redacted server-log tail before stopping and removing its own cluster.

## Title render visibility qualification

The fixed-region average from the earlier title test did not describe the authored
callout position. The exact retained document rendered natively in 344 ms; the old
sample averaged RGB 237/49/5 and failed its red-channel cutoff while visible text
and a panel existed to its right. No title clamping or production rendering change
was inferred from that test: freely positioned titles may extend beyond the canvas.

The focused workflow now compares against the source-only frame rendered by the
same native pipeline. It requires visible near-white glyphs and a dark panel in
the authored in-canvas region, plus an unchanged left-side background. Controls
reject no title, a textless panel and a wrongly positioned callout. The registered
focused CTest passes in 4.11 seconds: 907 glyph pixels, 5,771 panel pixels and all
19,200 reference background pixels unchanged. Missing-font fallback also renders.
The broad workflow retains real queued export, exact output publication and all
recipe/reopen/artifact checks. Its final queued-path result is recorded separately;
this focused pass does not establish all desktop or clean-machine acceptance.

The final broad queued timeline run **passes in 389.05 seconds**, including the
same 907/5,771/19,200 title evidence, original role/mutation guards and all later
workflow checks. Both focused and broad temporary PostgreSQL clusters were
stopped and their isolated database data removed; synthetic image/video proof was
retained. This remains local functional evidence, rather than installed-app or
physical-display qualification.

The current Linux main 92-case suite passes remotely, as do Windows headless,
Linux ThreadSanitizer and exported-source closure. Linux's extra deliberately
no-ICC configuration inherited the preset's strict-backend requirement despite
explicitly disabling the backend. That negative test now also disables strict
version enforcement for its own build directory. The normal production/CI preset
still requires the validated ICC backend. The local no-ICC target builds and its
`--expect-no-icc-backend` check passes without changing production requirements.

Local dependency qualification is limited to this host: link diagnostics show
some Homebrew libraries built for macOS 26 while CMake declares a 13.0 deployment
target. Before any public Mac installer is qualified, bundled dependencies must
meet its declared minimum OS and pass clean-machine checks. Neither signing nor
this local successful build alone proves compatibility with older Macs.

The final Rust release workspace/FFI rebuild with the TCP-only startup fix passes
(5m22s), and the macOS app target completes successfully. Rust CLI helpers are
installed by the packaging stage; a build-tree `.app` is not the fresh managed,
self-contained staging installer required for the customer walkthrough.
