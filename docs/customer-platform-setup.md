# Customer accounts, guests, keys and roles

The service implements verified email-code sign-in/recovery, device-scoped sessions,
independent Looksy account IDs, Paddle checkout/portal/webhook fulfillment, and
owner-managed complimentary VIP codes. The existing Lemon Squeezy adapter remains
for legacy purchases. These implementations are locally tested; hosted email,
merchant sandbox delivery and a fresh installed staging app still need acceptance
in the [production launch checklist](production-launch-checklist.md).

Customer sign-in uses an eight-digit code sent through Resend to a verified sender
address. Codes expire after ten minutes, permit five attempts, are bound to the
requesting installation, and are stored as keyed hashes. Delivery is throttled by
IP, email and installation. The OS-held installation bearer is the account session;
only its hash is stored in D1. Verified account sessions last thirty days; leases
never outlast the session, and a fresh email code renews it. Sign-out revokes that installation and the desktop
removes its credential and cached lease. Account recovery on another installation
requires another verified email code. Signing in never creates paid access or
restarts an expired account trial. Signing out is required before changing accounts.
Purchase email is metadata and never proves purchase ownership.

## Access matrix

| Access | Identity | Permissions |
| --- | --- | --- |
| Guest trial | Random installation credential in OS secure storage | New editing and agent work for 14 days by default |
| Paid editor | Verified purchase, configured variant features | `editor` enables editing; include `agent` to enable assistant work |
| Expired access | Existing installation | View, save and export existing work; no new edits or agent work |
| Support | Verified Cloudflare Access subject enrolled as `support` | Inspect customers, revoke devices, extend guest trials by up to 14 days per action |
| Admin | Verified Cloudflare Access subject enrolled as `admin` | Support actions, trial extensions up to 60 days, block/restore licenses, account session controls, VIP create/revoke/reissue |
| VIP customer | Verified Looksy account plus redeemed complimentary code | Only the configured product features, major version and expiry; never staff authority |

`beta` is a reserved customer feature; update eligibility currently follows the
build channel. Customer capabilities never confer staff authority. Product
features are configured in `productPolicy` and signed into device-bound leases.
Paddle purchases are linked to the signed-in account using a server-created checkout
record and verified merchant state. Legacy keys require live provider validation;
a linked purchase cannot move to a different account using only the key.

## Prepare staging

Run from `services/customer-platform`. Use the checked-in dependency lock and
the Node version described in its README. These first commands only prepare
files and check local configuration.

```sh
cp setup.example.json setup.local.json
```

Edit `setup.local.json`: set your exact HTTPS API origin, existing staging D1
database ID, Access team domain/application audience, verified Resend sender
`emailFrom`, an actual HTTPS sandbox `paddleCheckoutOrigin`, and real Paddle `pri_`/`pro_` mappings.
The checked-in sample uses `billingProvider: "paddle"`; replace every placeholder.
Legacy store setup can explicitly use `billingProvider: "lemon"`, a numeric
`storeId`, and numeric variant/product mappings. Keep both perpetual and subscription products. Set a distinct staging
key ID; use a dedicated private directory outside this checkout for keys. Its
parent directory must already exist; an existing key directory must be private
(mode 700).

```sh
npm run setup:prepare -- --input setup.local.json --output wrangler.staging.local.jsonc --key-dir /private/path/looksy-staging
```

The command writes an isolated staging Wrangler configuration, a private
Ed25519 PEM, its desktop public configuration, an outside-checkout `secrets.json`
containing generated signing/webhook/rate-limit secrets, and
`desktop-customer.local.cmake` containing only desktop public settings. It refuses
existing output files. The staging configuration permits test purchases and
keeps the default checked-in configuration intact. `routingMode` defaults to
`custom-domain`; `workers-dev` is an explicit private staging exception described
below. Preview URLs remain disabled in both modes.

Add `RESEND_API_KEY`, `PADDLE_API_KEY` and the actual Paddle notification
`PADDLE_WEBHOOK_SECRET` to private `secrets.json` using your secret manager or editor.
The Paddle secret comes from its notification destination; it is not locally invented.
Legacy stores additionally need `LEMON_API_KEY` and the generated Lemon webhook secret. Keep the file mode 600;
never paste its contents into chat, commit it, or pass secret values as command
arguments. Preserve an encrypted backup of the signing key before issuing clients.

```sh
npm run setup:check -- --config wrangler.staging.local.jsonc --public-key-file /private/path/looksy-staging/looksy-staging-1.public.json --secrets-file /private/path/looksy-staging/secrets.json
npm run check
npx wrangler deploy --dry-run --config wrangler.staging.local.jsonc
```

The checker verifies policy ranges, placeholders, staff authentication settings,
environment mode, perpetual major compatibility and signing-key consistency.
The default app version comes from `VERSION`; future release preparation requires
explicit `--app-version X.Y.Z` for both preparation and checking. Optional
`trustedKeys` holds up to four key IDs mapped to public Ed25519 keys for rotation
overlap; no private keys belong there. It prints field names rather than
values. It is an offline check: it cannot prove the remote database exists,
secrets are deployed, sender DNS is verified, MFA is enforced, staff are enrolled,
or checkout and email delivery work.

## Private staging without a domain

A purchased domain is not required for the owner's private account/VIP and Paddle
sandbox tests. The default custom-domain setup remains unchanged. To choose the
private path, edit these fields in the uncommitted staging input:

```json
{
  "routingMode": "workers-dev",
  "issuer": "https://looksy-customers-staging.YOUR-ACCOUNT-SUBDOMAIN.workers.dev",
  "emailFrom": "onboarding@resend.dev",
  "emailTestRecipient": "YOUR-REGISTERED-RESEND-EMAIL"
}
```

Replace both uppercase placeholders with actual account values. The issuer must
be the exact generated worker name `looksy-customers-staging`, your Cloudflare
account's workers.dev subdomain, HTTPS and no port/path/trailing slash. The
preparation command still writes staging-only policy; it enables `workers_dev`
only for this explicit mode and keeps preview URLs off. `setup:check` rejects
this route in production, a wrong worker hostname, or missing staff settings.
Cloudflare documents [the workers.dev hostname format and Access support](https://developers.cloudflare.com/workers/configuration/routing/workers-dev/).

Use the email registered to the Resend account as `emailTestRecipient`. Resend's
[onboarding sender can only deliver to that account's own email](https://resend.com/docs/knowledge-base/403-error-resend-dev-domain).
The Worker enforces the same recipient restriction before sending. It will not
send codes to other customers through that sender, and it rejects the sender in
live mode. A verified email domain is needed to invite other testers or launch
public email sign-in. The setup tools do not send an email.

Staff protection is still required: configure an Access application/policy for
the exact staging hostname's `/admin*` paths with MFA, set its audience/team,
and enroll the owner's trusted subject using the operator tool. The Worker still
validates the Access JWT and the enrolled role on every staff route. Do not put a
blanket interactive staff login on `/v1/*`; native email sign-in and merchant
webhooks use their own authentication. Before deploying, verify the actual Access
configuration protects staff pages while native API and notification requests can
reach their handlers. No Access policy is automatically created by preparation.

Paddle's [sandbox](https://developer.paddle.com/sdks/sandbox/) needs no website
approval. Its [default checkout URL settings](https://developer.paddle.com/api-reference/settings/update-checkout-url/)
accept any HTTPS URL in sandbox; live accounts require an approved domain. Set
`paddleCheckoutOrigin` to an actual HTTPS page containing the existing Paddle.js
checkout integration, such as a reviewed staging website on a hosting provider's
subdomain. The customer API Worker does not serve a public checkout page, so its
API origin alone is not a working checkout URL. Keep the sandbox client token,
products, prices and notification destination separate from live configuration.

When moving to production, use a reviewed custom-domain route with `workers_dev`
and preview URLs disabled, a verified sender domain, approved Paddle checkout
setup and separate live credentials/database/signing keys. Neither the no-domain
path nor a successful offline setup check is proof of hosted deployment or email
and payment delivery.

## Enroll staff

Configure a Cloudflare Access application protecting `/admin*` with MFA. Set its
exact audience/team domain in the setup input. Keep `/v1/*` available to desktop
clients without interactive staff login. Retrieve staff subject IDs from the
trusted Access identity; an email typed into the dashboard is not an identity.

Generate a reviewed enrollment file:

```sh
npm run setup:staff -- grant --subject STAFF_ACCESS_SUBJECT --role support --actor OPERATOR_ACCESS_SUBJECT --reason "Enroll customer support" --out support.local.sql
```

Use `--role admin` to enroll an administrator. A grant preserves an existing
different role; changing a role requires an explicit revoke followed by a grant.
Revoke preserves the last administrator. Review the subject and intended role.
To remove staff access:

```sh
npm run setup:staff -- revoke --subject STAFF_ACCESS_SUBJECT --actor OPERATOR_ACCESS_SUBJECT --reason "Remove staff access" --out revoke.local.sql
```

These commands write SQL plus an audit record and contact no database. The actor
label records the operator for audit purposes; it does not authenticate the
generator. Only the authenticated Cloudflare operator can apply the file. Generated
files contain Access subject identifiers, so retain them with restricted access.

## Apply the reviewed staging configuration

The commands below change remote staging resources. Run them after reviewing the
generated files and selecting the intended Cloudflare account. Create your staging
D1 database first if needed, then use its ID in the preparation input.

```sh
npx wrangler d1 migrations apply DB --remote --config wrangler.staging.local.jsonc
npx wrangler deploy --config wrangler.staging.local.jsonc --secrets-file /private/path/looksy-staging/secrets.json
npx wrangler d1 execute DB --remote --config wrangler.staging.local.jsonc --file support.local.sql
```

Add a Worker route/custom domain matching `issuer` before deployment; the generated
default custom-domain configuration disables `workers.dev` and preview URLs.
The explicit no-domain staging mode enables only its exact workers.dev host;
preview URLs remain disabled. Configure a Paddle sandbox
notification destination at `ISSUER/v1/webhooks/paddle` for transaction, subscription
and adjustment events, and copy its notification secret into the Worker secrets.
Use a separate verified Resend sender/domain for staging, or the owner-only
onboarding sender described above; configure `EMAIL_FROM`, any required
`EMAIL_TEST_RECIPIENT`, and a sending-only API key. No email is sent by the preparation commands.
Legacy Lemon stores use `ISSUER/v1/webhooks/lemon`. The route, sender DNS and
merchant fulfillment are operator configuration, not generated resources.

Wrangler supports selected-config D1 migrations and file execution, and secret-file
upload on deployment. See [D1 commands](https://developers.cloudflare.com/d1/wrangler-commands/)
and [Worker secrets](https://developers.cloudflare.com/workers/configuration/secrets/).

From the repository root, configure a separate managed desktop build with the
public settings file, retaining your regular build:

```sh
cmake --preset dev-macos-debug -B build/customer-staging -C services/customer-platform/desktop-customer.local.cmake
cmake --build build/customer-staging
```

## Verify before enabling customers

Exercise guest start, repeat start, expired-trial behavior, both test purchases,
the configured device limit, signed lease refresh, refund/cancellation and provider
outage. Verify guest/editor-only customers cannot enter staff routes or acquire
agent/admin capabilities. Check support cannot block licenses, admin actions are
audited, and revoked staff cannot open the dashboard. Reopen and export existing
work after editing expires. Local synthetic tests do not prove provider delivery
or signed desktop staging activation.

Production needs its own database, issuer, signing key, secrets, Access application
and live store/product mappings with `ALLOW_TEST_PURCHASES=false`. The preparation
tool deliberately generates staging only. The checker can validate an explicitly
prepared production config with `--environment production`; it does not convert
staging resources to production. Follow the remaining launch gates in the
[service runbook](../services/customer-platform/README.md), including existing
customer rights, terms, signing and recovery.

## Additional readiness implementation

Account controls include verified email sign-in/recovery, sign-out, linked license
selection, VIP redemption, device listing, confirmed revoke/deactivate actions,
verified purchase recovery with a fresh secure credential, and configured
checkout/billing links. Recovery retains revoked records and never restarts guest
access. Generated CMake includes the mandatory major guard and optional key map.

The [service runbook](../services/customer-platform/README.md) documents operations
alerts, bounded webhook retries, scoped privacy processing, private backups and
isolated local D1 restore rehearsal. Apply **all** migrations, including
`0002_operations.sql`, `0003_accounts_vip.sql` and `0004_paddle.sql`, before running the updated Worker. Real resources,
staff/service-token enrollment, fulfillment and Apple signing remain operator setup.

## Setup verification on 2026-10-05

The initial setup results below were followed by additional implementation and
verification. See [the readiness fixes and remaining launch work](customer-readiness-fixes-2026-10-05.md)
for the final test counts, account UI evidence and packaged-runtime proof limits.

Typecheck, 19 setup tests and 28 Worker/D1 integration tests passed. The separate
opt-in staff SQL test passed against local Wrangler D1, including audit-failure
rollback. Run it with:

```sh
LOOKSY_TEST_WRANGLER_D1=1 npm run test:setup
```

A generated temporary staging fixture passed the checker CLI, public CMake cache
parsing and Worker deployment dry-run. Fixtures and ephemeral keys were removed.
Existing desktop build tests for signed leases, account panel, expired document
workflow and automation access passed. This verifies the local setup tooling and
existing tested flows; no hosted resources were deployed and no live purchase,
email sign-in or managed desktop staging workflow was proved.

## Owner account and VIP tools

The Access-protected dashboard lists independent Looksy accounts alongside legacy
customer records. Admins can disable/enable accounts, revoke all account sessions,
and anonymize account contact data. Support cannot perform these owner actions
or mint complimentary codes. Staff enrollment remains the audited operator tool;
customer or VIP sign-in cannot enroll staff.

Create a VIP code with a reason, plan name, explicit feature list, covered app major,
redemption deadline, access end date, account redemption limit and device limit.
The high-entropy code appears once; only its hash is stored. Each account redeems
at most once, and concurrent redemptions enforce the total cap in the database.
Revoke stops future redemption and fresh leases for existing grants. Reissue
revokes the old code and grants and creates a replacement code with the same policy;
customers must redeem the replacement. An expired code requires a new policy.

Already signed offline leases remain valid until their original expiry (seven
days by default, capped by the access end date). Revocation cannot retract a lease
from an offline app. Another valid linked entitlement can be selected in Account;
revocation does not silently grant a new trial. The reserved `beta` feature does
not by itself authorize a private update feed.

Local Worker/D1 tests exercise verification/replay/expiry/throttling, account and
staff boundaries, paid-key ownership, privacy processing, VIP concurrency and
revocation. They use synthetic email/billing responses and ephemeral keys. Real
email delivery, merchant sandbox and signed installed-app testing remain required.
