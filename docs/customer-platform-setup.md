# Customer accounts, guests, keys and roles

The existing account model uses guest trials and purchase license keys. A guest
is an anonymous installation; a paid customer is identified by the verified
Lemon Squeezy purchase. Email sign-in, passwords and cross-device guest recovery
are not implemented. Customer email in the operations database is purchase
metadata and does not grant access.

## Access matrix

| Access | Identity | Permissions |
| --- | --- | --- |
| Guest trial | Random installation credential in OS secure storage | New editing and agent work for 14 days by default |
| Paid editor | Verified purchase, configured variant features | `editor` enables editing; include `agent` to enable assistant work |
| Expired access | Existing installation | View, save and export existing work; no new edits or agent work |
| Support | Verified Cloudflare Access subject enrolled as `support` | Inspect customers, revoke devices, extend guest trials by up to 14 days per action |
| Admin | Verified Cloudflare Access subject enrolled as `admin` | Support actions, trial extensions up to 60 days per action, block/restore licenses |

`beta` is a reserved customer feature; update eligibility currently follows the
build channel. Customer capabilities never confer staff authority. Product
features are configured in `productPolicy` and signed into device-bound leases.
Customer activation keys come from Lemon Squeezy license-key generation and email
delivery; the setup command does not invent paid license keys.

## Prepare staging

Run from `services/customer-platform`. Use the checked-in dependency lock and
the Node version described in its README. These first commands only prepare
files and check local configuration.

```sh
cp setup.example.json setup.local.json
```

Edit `setup.local.json`: set your exact HTTPS API origin, existing staging D1
database ID, Access team domain/application audience, Lemon store ID and real
product/variant mappings. The sample IDs `101`, `102`, `10` and `11` must be
replaced. Keep both perpetual and subscription products. Set a distinct staging
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
keeps the default checked-in configuration intact.

Add `LEMON_API_KEY` to the private `secrets.json` using your secret manager or
editor. This is the provider's actual API credential. Keep the file mode 600;
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
secrets are deployed, MFA is enforced, staff are enrolled, or purchases deliver keys.

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
configuration disables `workers.dev` and preview URLs. In Lemon Squeezy, enable
license keys and email delivery for both configured products and set the signed
webhook endpoint to `ISSUER/v1/webhooks/lemon` using the generated webhook secret.
The route and provider fulfillment are operator configuration, not generated resources.

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

Account controls include device listing, confirmed revoke/deactivate actions,
verified purchase recovery with a fresh secure credential, and configured
checkout/billing links. Recovery retains revoked records and never restarts guest
access. Generated CMake includes the mandatory major guard and optional key map.

The [service runbook](../services/customer-platform/README.md) documents operations
alerts, bounded webhook retries, scoped privacy processing, private backups and
isolated local D1 restore rehearsal. Apply **all** migrations, including
`0002_operations.sql`, before running the updated Worker. Real resources,
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
