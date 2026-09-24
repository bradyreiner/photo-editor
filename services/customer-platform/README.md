# Looksy customer operations

Worker + D1 service, Cloudflare Access staff dashboard, Lemon Squeezy purchase
validation, and device-bound Ed25519 desktop entitlements. No production resources
have been created. The checked-in configuration intentionally cannot issue leases.

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
rights already granted by an earlier receipt-only license. Before enabling the
managed-license build for sale, finish the policy changes listed below.

## Local verification

Use Node 24. `npm ci --legacy-peer-deps`, then `npm run check`. Tests run a real
local D1/Worker runtime with synthetic provider responses and ephemeral signing
keys. No billing account or remote database is used. The workerd override keeps
the test runtime compatible with the configured date; the sharp override removes
the older transitive dependency. Revalidate both when updating the test plugin.

`npx wrangler deploy --dry-run` checks the deployable bundle without deployment.
`node scripts/preview-admin.mjs` serves synthetic dashboard records on loopback
port 8788; it is only a visual fixture and never an authentication fallback.

## Configure an isolated staging environment

1. Create a D1 database and put its real ID in a staging copy of `wrangler.jsonc`.
   Set an HTTPS origin as `ISSUER`. The origin must exactly match the desktop
   `LOOKSY_CUSTOMER_API_URL`, including any port and with no trailing slash/path.
2. Generate an Ed25519 signing key outside the repository:
   `node scripts/generate-signing-key.mjs /secure/looksy-staging looksy-1`.
   Back up the PEM in your secret manager. The `.public.json` file contains the
   only key material that belongs in the desktop build.
3. Put `ENTITLEMENT_PRIVATE_KEY`, `LEMON_API_KEY`, `LEMON_WEBHOOK_SECRET`, and
   `RATE_LIMIT_SECRET` into Worker secrets using `wrangler secret put`. For PEM,
   redirect the key file into stdin. Never pass secret values as CLI arguments.
   `.dev.vars.example` documents local names; `.dev.vars` is ignored.
4. Set the store ID and explicit variant policies. Example shape (replace all IDs):

   ```json
   {
     "101": {"productId":"10","plan":"studio","mode":"perpetual","major":1,"features":["editor","agent"],"deviceLimit":2},
     "102": {"productId":"11","plan":"studio-monthly","mode":"subscription","major":1,"features":["editor","agent"],"deviceLimit":2}
   }
   ```

   Enable license-key generation and key delivery for both provider products.
   Set `ALLOW_TEST_PURCHASES=true` only in staging. The production environment
   must have separate database, keys, secrets and store mappings, with this false.
5. Apply `migrations/0001_customers.sql` with `wrangler d1 migrations apply` against
   the intended environment. These tables must never be added to a local photo
   catalog. The only schema in this service is hosted customer operations.
6. Create an Access application for `/admin*`, enforce MFA, and set its exact
   `ACCESS_TEAM_DOMAIN` and `ACCESS_AUDIENCE`. Enroll staff subjects in the `staff`
   table through the operator's authenticated Cloudflare account. Use an Access
   subject ID, not an email supplied by the browser. Missing roles fail closed.
   Do not protect `/v1/*` with an interactive staff login.
7. Configure the provider webhook URL `/v1/webhooks/lemon` with its secret and
   the order, license, subscription and subscription-payment events supported in
   `src/index.ts`. A missing initial subscription event is recovered by an exact
   order-item subscription query; invalid mappings still fail closed.
8. Deploy staging only after reviewing the configuration. Set both checkout URLs
   and price labels in the release configuration; keep commercial approval false
   until the purchase, cancellation, refund and recovery checks pass.

Canonical state is fetched from the provider, not trusted from event attributes.
Duplicates are deduplicated by digest. Failed receipts retry hourly, with a cap
of 20 per invocation; 20 stale licenses are reconciled per invocation. At larger
volumes replace this bounded polling with a queue and monitor backlog age.

## Desktop build

Configure `LOOKSY_CUSTOMER_API_URL`, `LOOKSY_ENTITLEMENT_PUBLIC_KEY` (base64 raw
32-byte public key), and `LOOKSY_ENTITLEMENT_KEY_ID`. OpenSSL Crypto is required.
An empty API URL deliberately retains the existing receipt-only build. Invalid
managed configuration cannot unlock editing. Never ship a private signing key.

The installation bearer credential lives in macOS Keychain or Windows Credential
Manager, and only its hash is stored on the server. Cached signed leases use app
preferences. A failed activation of a different key preserves valid existing
access; a confirmed refresh revocation removes it. An outage never extends the
original signed expiry. Customer IDs come from the provider; raw license keys
are transient request fields and are not stored in D1 or diagnostic logs.

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
recovery. Restore into staging, compare customer/license/device counts, replay
billing state and staff revocations, then test leases before switching traffic.
Restoring an older database must not silently undo refunds or support blocks.
Back up signing keys separately. The current app pins one key: rotation requires
an app update/overlap migration; losing the key is not fixed by changing a server
environment variable. Plan this before issuing long-lived public clients.

Rate-limit buckets expire after two minutes; successful webhook receipts after
90 days. Customer/license/device and staff audit records are retained until an
operator processes a verified retention/deletion request. No automatic customer
purge or legally determined retention schedule is configured. For a request,
verify purchase ownership, export only that customer's records, revoke devices,
and coordinate provider/Sentry deletion subject to required transaction records.
Use a reviewed database transaction and preserve the deletion audit separately.

## Remaining launch gates

- Real products, prices, key-email delivery and both checkout journeys.
- New terms/privacy/refund disclosures for activation, recurring billing,
  offline grace, device limits, diagnostic processor/retention and cancellation.
  `LICENSE` and current terms still describe the earlier receipt-only perpetual
  offer: revise them for new purchases and explicitly preserve existing rights.
  Do not approve commerce while this discrepancy remains.
- A migration/entitlement path for any existing receipt-only customers.
- Live Access/MFA, billing webhooks and outages, signed desktop activation,
  device recovery, and data deletion/restore exercises.
- Packaged crash capture + symbolication; signed two-version updater proof;
  separate Windows clean-machine/runtime proof.

Primary references: [Lemon Squeezy subscription filtering](https://docs.lemonsqueezy.com/api/subscriptions/list-all-subscriptions),
[Sparkle publishing](https://sparkle-project.org/documentation/publishing/),
[D1 recovery](https://developers.cloudflare.com/d1/reference/time-travel/).
