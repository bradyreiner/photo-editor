# Looksy customer operations

Worker + D1 customer accounts, Cloudflare Access staff tools, Paddle billing and
legacy Lemon Squeezy validation, with device-bound Ed25519 desktop entitlements.
Verified email-code sign-in/recovery uses a configured Resend sender. Admins can
create, revoke and reissue scoped VIP codes and manage customer sessions. No hosted
resources, actual emails or merchant transactions are created by these source changes.

Start with [Customer platform setup](../../docs/customer-platform-setup.md) for the
current Paddle staging walkthrough and account/VIP behavior. `setup:prepare` writes
private staging scaffolding, `setup:check` validates it offline, and `setup:staff`
generates audited staff enrollment SQL. The checked-in configuration intentionally
fails closed. Follow the [launch checklist](../../docs/production-launch-checklist.md)
for installed-app, live delivery and signed-release acceptance.

## Product policy

- Both one-time licenses and subscriptions. A perpetual license covers its
  purchased major version; a subscription follows the currently verified plan.
- Default guest trial: 14 days, anonymous installation identity. Two devices per
  configured paid license; paid offline access is bounded to seven days.
- Expiry blocks new content edits and new AI runs. Opening existing documents,
  saving, exporting, finishing an admitted operation, and saving an existing AI
  draft remain available. Never delete customer documents on expiry.
- Editing privileges are `editor` and `agent`. The `beta` claim is reserved;
  updater eligibility currently follows the build channel. Customer features never confer staff
  authority. `support` can revoke devices or extend a trial by up to 14 days per
  action. `admin` can also block/unblock a license or extend by up to 60 days.
- A canceled subscription retains access through its verified paid-through date.
  `past_due` follows the provider's retry period; `unpaid`, paused and expired
  subscriptions are denied. Refunds/disabled licenses are denied on refresh.
- An installation is not a verified human. A person can reset their identity;
  this design deliberately avoids hardware serials and invasive fingerprinting.

These are implementation defaults, not published pricing or a replacement for
rights already granted by an earlier receipt-only license. Revised LICENSE and
website policies distinguish managed purchases and preserve earlier rights;
commercial approval remains false until operator review and approval.

## Local verification

Use Node 24. `npm ci --legacy-peer-deps`, then `npm run check`. Tests run a real
local D1/Worker runtime with synthetic provider responses and ephemeral signing
keys. No billing account or remote database is used. The workerd override keeps
the test runtime compatible with the configured date; the sharp override removes
the older transitive dependency. Revalidate both when updating the test plugin.

`npx wrangler deploy --dry-run` checks the deployable bundle without deployment.
`node scripts/preview-admin.mjs` serves synthetic dashboard records on loopback
port 8788; it is only a visual fixture and never an authentication fallback.

## Configure isolated staging

Use [the staging walkthrough](../../docs/customer-platform-setup.md#prepare-staging)
and `setup.example.json`. Set `billingProvider: "paddle"`, real sandbox `pri_` and
`pro_` policy mappings, both purchase offers, an approved checkout origin, a verified
sender email, D1/Access identifiers and a distinct signing key ID. Keep the database,
merchant account credentials, sender, signing keys and domain separate from production.

Add actual `RESEND_API_KEY`, `PADDLE_API_KEY`, `PADDLE_WEBHOOK_SECRET` to the private
secrets file. Paddle supplies its notification secret; preparation generates only
Looksy signing/rate-limit keys and the optional legacy Lemon webhook secret.
Apply every migration, including accounts/VIP and Paddle. Enforce Access/MFA on
`/admin*`, not customer `/v1/*` routes. Nothing here deploys automatically.

Paddle checkout is created by the authenticated service for one fixed offer and
quantity. Fulfillment requires the matching stored checkout, exact owner/product
and completed provider payment; caller metadata or email never grants access.
Portal URLs are authenticated and constrained to Paddle's approved host. Webhooks
are signed, deduplicated and refresh canonical provider state; the hourly job also
recovers missed checkout fulfillment. Partial refunds are denied until reviewed.
The legacy Lemon adapter remains available for existing verified keys, with
numeric `storeId` and explicit policies when `billingProvider: "lemon"` is chosen.

Canonical state is fetched from the provider, not trusted from event attributes.
Duplicates are deduplicated by digest. Failed receipts persist attempts and a
retry deadline, with exponential backoff and a maximum of eight failed attempts.
The hourly job processes at most 20 due receipts and 20 stale licenses. Exhausted
receipts need an audited manual retry. At larger volumes use a queue and monitor backlog age.

## Desktop build

Configure `LOOKSY_CUSTOMER_API_URL`, `LOOKSY_ENTITLEMENT_PUBLIC_KEY` (base64 raw
32-byte public key), `LOOKSY_ENTITLEMENT_KEY_ID`, and
`LOOKSY_CUSTOMER_POLICY_MAJOR` matching the desktop major in `VERSION`. Setup
checks every perpetual variant against that major; explicit `--app-version X.Y.Z`
prepares a separate future release. OpenSSL Crypto is required.
An empty API URL deliberately retains the existing receipt-only build. Invalid
managed configuration cannot unlock editing. Never ship a private signing key.

The installation bearer credential lives in macOS Keychain or Windows Credential
Manager, and only its hash is stored on the server. Cached signed leases use app
preferences. A failed activation of a different key preserves valid existing
access; a confirmed refresh revocation removes it. An outage never extends the
original signed expiry. Looksy account IDs are independent of merchant customer IDs; raw license keys
are transient request fields and are not stored in D1 or diagnostic logs.

The Account panel lists devices and confirms revoke/deactivate actions. Purchase
recovery verifies the key, creates a fresh random bearer, retains the old
installation as revoked, and respects the paid device cap. The desktop replaces
secure credentials and cached access only after verifying the returned signed
lease. Recovery does not restart a trial. Set `LOOKSY_PURCHASE_URL`,
`LOOKSY_SUBSCRIPTION_PURCHASE_URL` and `LOOKSY_BILLING_PORTAL_URL` for reviewed
HTTPS checkout and hosted subscription management links. Customers complete
payment changes and cancellation in the hosted portal.

Rotation supports up to four additional compiled public keys in
`LOOKSY_ENTITLEMENT_TRUSTED_KEYS_JSON` (`{"previous-kid":"base64-public-key"}`).
Ship an overlap client trusting the next public key before switching the Worker
signer, verify both key IDs in staging, then retire the old key in a later client.
Old clients that never received the next public key cannot verify its leases.
Setup input `trustedKeys` writes public overlap metadata and the CMake cache;
the Worker still signs with one selected `SIGNING_KEY_ID`/private key.

The admission inventory is `release/customer-edit-admission.json`; run
`scripts/check-customer-edit-admission.py` when adding QML APIs. Review every
exemption. Controller checks precede mutations, including native callers and
automation; they do not depend on disabled UI controls. Do not remove a guard to
make a test pass. The native expiry workflow test creates and reopens actual
photo, board, and timeline work and exports after disabling editing access.

## Crashes and support logs

Crash reporting is optional and off by default. Build Sentry Native 0.17 with its
Crashpad backend and set `LOOKSY_ENABLE_CRASH_REPORTING=ON`, `LOOKSY_SENTRY_DSN`,
`LOOKSY_CRASHPAD_HANDLER` from that same build, and `LOOKSY_RELEASE_CHANNEL`.
The handler is copied beside the executable and must be signed with the package.
Bundle the SDK's runtime dependencies and license notices. The adapter was
syntax-checked against the 0.17 public header; SDK linking, packaged capture,
upload and symbolication still require the actual release SDK/project.

Reports use release/channel and an opaque verified customer ID, with fixed
workflow event names. They do not attach Qt logs, photo/catalog files, or input
text. Native dumps can contain process memory: do not claim they contain no
personal information. Retain the existing user-created support-bundle flow for
detailed logs; review paths and other sensitive content before sharing.

Disabling reporting revokes SDK consent before closing and removes its dedicated
queue, reporting cleanup failures. Envelope cache limits are 20 items, 20 MiB and
seven days at startup; the complete dump directory is cleared at startup if over
50 MiB. These are startup bounds, not a hard cap during a running process.

Keep dSYMs/PDBs for every exact shipped binary, including helpers. Upload matching
debug files to the configured Sentry project before rollout. Prove a controlled
crash in an isolated, signed candidate; check readable native frames, release,
customer ID, and consent-off/no-upload behavior. Helper crashes and GPU hangs
need their own capture proof; the desktop adapter alone does not prove them.

## Updates, operations, and recovery

Keep the existing Sparkle signing/notarization and Windows AppInstaller flows.
Use a feed per major version so a perpetual customer is not auto-upgraded into a
major version they do not own. Stable builds exclude beta-tagged releases; beta
builds allow beta and stable updates. After generating a signed appcast:

```sh
python3 scripts/configure-appcast-rollout.py path/to/appcast.xml --version 1.0.1 --interval 86400
# To stop offering that release, remove it from the feed and republish the feed:
python3 scripts/configure-appcast-rollout.py path/to/appcast.xml --version 1.0.1 --halt
```

Phasing affects automatic checks; Sparkle manual/critical updates bypass it.
Removing a release prevents new offers; it cannot retract an installed/downloaded
update. Recovery should ship a corrected higher build, with a compatible catalog
or tested backup restoration. Prove update, interruption, restart and recovery
using two signed versions on a clean machine before production.

Monitor license API error rate/latency, failed renewals, webhook backlog age,
revocation lag, crash-free sessions per release, and update adoption from device
version/last-seen records. Configure provider budget alerts and ingestion limits.
Customer identifiers in Sentry can be matched to the staff dashboard. Device
last-seen/version is refreshed at startup and every six hours, not continuous
user activity tracking. Staff access and actions need periodic review.

Use D1 Time Travel and an encrypted, access-controlled export for disaster
recovery. Create private artifacts outside this checkout:

```sh
# Use the real reviewed config; select local or remote explicitly.
npm run database:recovery -- backup --config wrangler.staging.local.jsonc --directory /private/backups/looksy-snapshot-new --remote
npm run database:recovery -- rehearse --backup /private/backups/looksy-snapshot-new --directory /private/backups/looksy-restore-new
```

Backup is read-only and checks SQLite integrity/foreign keys. Its manifest records
SHA-256 plus counts and content fingerprints for every table and schema/index/trigger
fingerprints. Restore creates every table before loading rows so ALTER-added
account foreign keys can reference tables that appear later in a D1 export. Rehearsal checks
the checksum before importing into fresh isolated local D1 state and comparing
every restored row. The required schema includes all account, authentication, VIP
and Paddle tables (18 service tables); a snapshot missing them is rejected. This
prevents a partial restore from silently losing sessions, grants or purchase links.
Older pre-account snapshots need a separately reviewed migration rehearsal.
It never restores remote data or overwrites existing artifacts.
Keep artifacts encrypted and apply a reviewed retention schedule. Before any
production recovery, replay current billing and staff/device revocations and test
leases in staging before switching traffic.
Restoring an older database must not silently undo refunds or support blocks.
Back up signing keys separately. Losing a key cannot be fixed just by changing a
server environment variable; use the compiled overlap procedure above.

IP rate-limit buckets expire after two minutes and email/device delivery buckets after fifteen minutes;
email challenges expire after ten minutes and are removed by the hourly job after one day; successful webhook receipts after
90 days. Customer/license/device and staff audit records are retained until an
operator processes a verified retention/deletion request. No legally determined
retention schedule is configured. Staff may export a scoped customer record;
admin-only anonymization removes personal fields, revokes installations and retains a
tombstone preventing reconciliation from repopulating email. Transaction IDs,
blocks, revocations and audit history remain for recovery/accounting needs. This
is anonymization, not deletion of every transaction. Verify ownership first,
review the audit reason for personal data and coordinate provider/Sentry deletion
separately. Backups and historical audit reasons need separate retention handling.

## Operational alerts

`GET /admin/api/operations` requires verified Access identity and an enrolled staff
role. It reports counts, retry/backlog age, stale license reconciliation and unfulfilled
Paddle checkouts. Pending checkout counts include unpaid/abandoned checkouts; they
are not proof of failed payments. More than 100 due checkouts raises a reconciliation
capacity alert; the hourly worker currently checks at most ten checkouts per run. Run
`npm run operations:check -- response.json` on a private response file, or
`npm run operations:monitor` with `LOOKSY_OPERATIONS_ORIGIN`,
`LOOKSY_ACCESS_CLIENT_ID` and `LOOKSY_ACCESS_CLIENT_SECRET` from a secret manager.
The monitor refuses redirects, bounds responses and prints only alert codes.
Exit 1 means operational alerts; exit 2 means invalid input/auth/service failure.

The `customer-operations.yml` workflow polls hourly after configuring the origin
repository variable and `LOOKSY_MONITOR_ACCESS_CLIENT_ID`/
`LOOKSY_MONITOR_ACCESS_CLIENT_SECRET` secrets. Create a dedicated Access service
token and a Service Auth policy on the same Access application as human staff.
Enroll `service:<CF-Access-Client-Id>` as support using the staff SQL tool.
The Worker verifies the signed `common_name` claim and confines service identities
to GET `/admin/api/operations`. Human identities keep their JWT `sub` and MFA.
See [Access application token claims](https://developers.cloudflare.com/cloudflare-one/access-controls/applications/http-apps/authorization-cookie/application-token/).
Enable repository
failure notifications. No remote monitor/token is configured by these changes.
Service health does not establish backup freshness: schedule encrypted backups
separately and review restore reports.

## Remaining launch gates

- Real Paddle sandbox/live products, prices, verified email delivery and both checkout journeys.
- Operator approval of the revised terms/privacy/refund disclosures for activation,
  recurring billing, offline grace, devices, retention and cancellation. Earlier
  receipt-only rights are preserved; commercial approval remains false.
- A migration/entitlement path for any existing receipt-only customers.
- Live Access/MFA, billing webhooks and outages, signed desktop activation,
  device recovery, and data deletion/restore exercises.
- Packaged crash capture + symbolication; signed two-version updater proof;
  separate Windows clean-machine/runtime proof.

Primary references: [Lemon Squeezy subscription filtering](https://docs.lemonsqueezy.com/api/subscriptions/list-all-subscriptions),
[Sparkle publishing](https://sparkle-project.org/documentation/publishing/),
[D1 recovery](https://developers.cloudflare.com/d1/reference/time-travel/).
