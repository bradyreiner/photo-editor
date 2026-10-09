# Customer readiness fixes — 2026-10-05

The local implementation now includes the account, recovery, operations and
configuration gaps found in the readiness review. Three Luna agents implemented
desktop account controls, backend recovery/operations, and admission/configuration
checks; the coordinating agent integrated and verified their changes. No hosted
resources, payment settings, staff enrollments or production secrets were changed.

## Implemented behavior

- Guest trials and purchase-key accounts retain distinct customer capabilities
  and staff support/admin roles. This is purchase-key activation and recovery;
  email/password login is not part of this account model.
- The account panel lists devices, confirms revoke/deactivate, and exposes
  configured perpetual checkout, subscription checkout and billing portal links.
  A verified recovery rotates the installation credential without restarting a
  trial. Failed verification or secure storage preserves existing access.
- Recovery retries reuse the same pending credential after a network or secure
  storage failure, making a completed server activation safe to retry. This
  pending candidate is held in memory: keep Looksy open and retry with the same
  purchase key after a storage error. After quitting, support may need to release
  the unresolved device seat before another recovery.
- Desktop lease verification supports a bounded compiled public-key map for
  signing-key overlap. Unknown keys, conflicting primary keys, malformed maps,
  wrong device/issuer, expired leases and clock rollback fail verification.
- Setup validates the current major version from `VERSION` (currently 0), product
  mappings, HTTPS checkout/billing URLs, public keys and private file boundaries.
  New managed builds must explicitly match their customer policy major.
- The edit-admission inventory checks mutation APIs and reviewed helper paths.
  Expired accounts preserve existing photos, typography, paint and timelines;
  viewing, saving and exporting remain available while new edits/agent work stop.
- Webhook retry state persists with backoff and an eight-attempt limit. Staff can
  inspect health; administrators can retry failures. Machine monitoring uses an
  enrolled signed Access service identity restricted to operations health.
- Scoped customer export, audited anonymization, provider tombstones and revoked
  device history support privacy operations without deleting local photos. Legal
  transaction/audit records remain retained as documented in the runbook.
- Private D1 backups include checksums and every table's content fingerprint.
  Restore rehearsals import into an isolated local database and compare all rows.
  An hourly CI workflow can alert on operational counts once configured.
- Terms, privacy, refunds and download/support pages describe the new managed
  offers while preserving existing receipt-only customer rights. Publication
  approval remains an explicit release configuration requirement.

Apply both `0001_customers.sql` and `0002_operations.sql` before running the new
Worker. Follow [the setup guide](customer-platform-setup.md) and
[the service runbook](../services/customer-platform/README.md).

## Verification and evidence

- Worker TypeScript check and 36 Worker/D1 tests passed.
- Setup/operations tooling: 32 passed, with two opt-in integrations skipped in
  the standard run. The separate local D1 backup/restore run passed all three
  tests, including content preservation, private permissions and corrupt-backup
  rejection. The staff SQL opt-in integration passed earlier in this task.
- All five native customer tests passed: leases, controller recovery, account
  panel, document expiry and automation admission. The expiry fixture was then
  made independent of network connection retries and passed again in 2.74 seconds.
- Admission audit and seven parser regression tests passed; 16 commerce tests,
  six download-page tests, two support-page tests and static site validation passed.
- The desktop application linked successfully, and the final Worker bundle
  deployment dry-run passed. No deployment occurred.
- Native account fixtures were rendered and visually inspected. Their
  [screenshots](evidence/customer-readiness-2026-10-05/README.md) use synthetic
  signed responses and an injected credential store, so they do not prove live
  provider fulfillment or OS credential-store behavior on a clean machine.
- An isolated packaged startup loaded QML, but the broader managed-runtime proof
  **failed** because the local AI runtime timed out. The intentionally offline
  catalog also reported its expected helper timeout. The proof child process
  stopped after exit. See the [compact receipt](evidence/customer-readiness-2026-10-05/startup-summary.json).

## Remaining launch work

Configure separate staging and production Cloudflare databases/domains, signing
secrets/public keys, verified Access policies and staff/service identities, real
Lemon Squeezy store/products/webhook delivery, checkout prices and billing portal.
The current desktop build has no customer API configured and retains receipt-only
behavior. Generated setup files are reviewable inputs, not deployed resources.

Exercise hosted guest activation, both test purchases, key delivery, device limits,
recovery, refunds/cancellation, retries and offline expiry from a managed desktop
build. Test the actual OS credential store and recovery across relaunch. Finalize
publisher/support/legal configuration and approve the commercial terms.

Public preflight currently fails on missing checkout URLs/prices/billing portal,
legal publisher, public release repository, website/support email, Sparkle feed,
Sparkle public key and distribution URL/archive. Apple signing/notarization,
signed update/rollback, clean-machine install/edit/export/relaunch and the failed
local AI startup proof remain separate qualification gates. Local test passes do
not establish readiness to sell.
