# Customer platform implementation

This implements the customer operations requested on 2026-09-23. The existing
receipt-only perpetual release remains the default until the hosted service,
product mappings, revised policies, and signed desktop configuration are ready.
No existing customer is silently converted to a subscription.

## Product rules

- Confirmed: offer both perpetual purchases and subscriptions as separate entitlements.
- Proposed launch defaults: a 14-day guest trial, two active devices per license,
  and at most seven days between paid entitlement refreshes. These are explicit
  server policy, not a clock stored in desktop preferences.
- A guest is an installation, not an identified person. Reinstall/identity reset
  resistance is best effort; do not collect hardware serials or promise fraud-proof trials.
- License-key possession activates a purchase. Billing-provider customer ID is
  the account identity. Purchase recovery stays with Lemon Squeezy. Email-only
  account login/SSO can be added without changing customer IDs.
- Keep customer capabilities separate from staff support/admin roles.
- Confirmed: never discard work on expiry. Viewing, saving and exporting existing
  work remain available; block new edits. Admission checks cover photo, board,
  timeline, scene, catalog recipe and AI/automation paths, with an explicit
  reviewed inventory and native expiry workflow proof.
- Existing local PostgreSQL catalogs remain local and unchanged. The hosted
  operations database is separate and contains no photo catalog.

## Work and acceptance

- [ ] Hosted service: persistent guest trials, validated purchases, signed leases,
  device management, provider webhooks, reconciliation, scoped staff actions.
- [ ] Desktop: secure installation credential, pinned lease verification,
  activation/trial/refresh UI, offline expiry and unavailable-service behavior.
- [ ] Privileges: one capability policy, enforced at operation admission, including
  agent/export routes; current work is never interrupted by expiry.
- [ ] Diagnostics: opt-in native crashes, matching symbols, bounded redacted
  context, helper failures, consent revocation and a real packaged crash proof.
- [ ] Operations: authenticated customer dashboard, audit trail, recovery and
  deletion runbook, retention, backups/restore, monitoring and cost controls.
- [ ] Distribution: signed production feeds, stable/beta eligibility, staged
  rollout, halt controls, two-version upgrade proof and catalog recovery.
- [ ] Commercial integration: checkout mappings, key delivery, recovery,
  cancellation/refund/renewal tests, revised website/policies and live proof.

## Required external configuration

Cloudflare account/domain and isolated staging/production databases; Lemon Squeezy
store/product/variant IDs and API/webhook secrets; entitlement signing key; staff
identity provider with MFA; crash-reporting project; Apple signing/notarization
and Sparkle credentials. Keep all secrets out of the app, source, logs and URLs.
Deployment is a separate action after the implementation and evidence can be reviewed.

## Local implementation evidence

The [service runbook](../services/customer-platform/README.md) includes setup,
policy defaults, operational recovery, and the remaining production gates.
Local verification on 2026-09-23:

- Worker typecheck and 24 local D1/Worker integration tests pass, including direct
  paid activation, missing subscription webhooks, concurrent device limits,
  delayed stale responses/refunds, signed staff roles and cross-origin denial.
- The desktop app builds. Native signed-lease verification, account QML rendering,
  and actual document expiry tests pass. Existing photo exports and board
  save/reopen/export succeed after editing admission is denied; timeline
  save/reopen succeeds and renewal restores edits.
- The native AI/automation/MCP access test passes: new edits are denied, while
  reading, export admission, and saving existing drafts remain available.
- Both checkout choices pass the 14 commerce tests and six download-page tests.
- Appcast policy preserves signed enclosure metadata across beta/stable and halt
  changes. Worker deployment dry-run succeeds; nothing was deployed.
- Account UI and the synthetic customer dashboard were rendered and inspected.
- The optional Sentry branch passes a syntax check against the 0.17 public header;
  it is disabled in the current build. This does not prove capture or delivery.

The broad acceptance checkboxes above deliberately remain open until live service,
packaged crash, signed-update and clean-machine evidence exists. The current
desktop has no configured customer endpoint, so receipt-only behavior is retained.

## Security and evidence

The desktop pins the entitlement public key and issuer. A server response is not
trusted merely because it says `active`. Device credentials are random, stored in
the OS credential store, and hashed on the server. License keys are transient and
never logged. Webhooks are authenticated, replay-safe and reconciled against
provider state. Staff requests require verified identity and an explicit role.
Offline revocation is bounded by lease lifetime; native code on a user-controlled
machine cannot be made unpatchable.

Tests must cover tampering, wrong audience/device/issuer, expiry, clock rollback,
repeated guest starts, device limits under concurrency, wrong-store/test keys,
duplicate/out-of-order webhooks, canceled-but-paid subscriptions, refunds,
unauthorized staff actions, consent and network failures. Local tests do not prove
production delivery, clean-machine installation or payment-provider fulfillment.
