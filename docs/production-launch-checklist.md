# Looksy Production Launch Checklist

The release targets **Apple silicon Macs and Windows x64**, distributed directly
from the website as a signed/notarized DMG and a signed MSIX/App Installer. The
configured offer includes subscriptions and one-time licenses. The current code
uses Lemon Squeezy, but the billing provider now requires a replacement decision.
See [commercial-launch.md](commercial-launch.md). Intel Mac items apply only when
`mac-intel` is added to `release/product.json`.


This is the release contract for turning a tagged commit into public,
upgradeable desktop installers. Do not mark an external item complete until the
credential, account, review, or clean-machine proof actually exists.

## Next milestone: private customer-system test release

Scope confirmed on 2026-11-17: real customer **email sign-in and account
recovery**, a downloadable Mac installer, account permissions, owner/admin
operations, VIP access-code creation/revocation, payments, error reporting and
desktop app updates. Email sign-in is required; the existing license-key-only
Account panel does not satisfy this scope. "Updates" here means desktop app
updates using Sparkle and Windows App Installer.

The first milestone is a private staging release that can complete the entire
customer journey. Apple enrollment and infrastructure setup can proceed while
the missing software and CI failures are addressed.

### Current state and implementation gaps

| Area | Verified current state | Required next work |
| --- | --- | --- |
| Customer identity | Guest trials and purchase-key activation/recovery exist; customer email is purchase metadata | Add verified email sign-in, session management, sign-out and recovery; link purchases to an independent Looksy account |
| Managed access | Device-bound signed leases and OS credential storage exist | Configure a real staging API and public verification key in a fresh installer; the current local build has neither |
| Owner and staff | Cloudflare Access identity plus enrolled `support`/`admin` roles exist in source | Enroll the owner, enforce MFA and test backend authorization with separate customer/support/admin identities |
| VIP codes | No independent admin-issued complimentary-license workflow | Implement audited creation, redemption, expiration, device/use limits, revocation and reissue; define version/feature coverage |
| Existing revocation | Device revocation and admin license blocking exist | Prove refresh denial and cached-lease expiry; customer polling is currently six hours and paid offline leases default to seven days |
| Billing | Lemon Squeezy-specific adapter; checkout prices/URLs are blank | Select the replacement provider and implement verified fulfillment, subscriptions, refunds and reconciliation |
| Error reports | Local diagnostics/support bundles exist; native crash reporting is off | Configure a reporting project, link/package/sign the real SDK and handler, upload matching symbols and prove delivery/consent behavior |
| App updates | Sparkle and App Installer wiring exist; this Mac build has Sparkle off and no feed/key | Configure private signed feeds and test two actual installer versions, interruption, recovery and account/data preservation |

"VIP code" means a complimentary product-access code. Staff roles and private
signing keys are separate resources. Staff enrollment/removal currently uses the
audited operator tool; a VIP customer never becomes an administrator.

Lemon Squeezy announced on **November 16, 2026** that new store sign-ups are
closed and payment processing ends **March 31, 2027**. A new launch must resolve
the provider choice before further payment integration. Existing-store migration
must preserve customer rights and purchase history. [Official announcement](https://www.lemonsqueezy.com/blog/saying-goodbye-to-lemon-squeezy).
Stripe Managed Payments is a candidate subject to eligibility review; it is not
a complete replacement for Lemon Squeezy's licensing/fulfillment features.
[Eligibility](https://docs.stripe.com/payments/managed-payments/eligibility),
[feature comparison](https://docs.lemonsqueezy.com/migration/comparison).

### Ordered implementation and setup

1. **Repair release gates.** Resolve the desktop regressions, code/package
   duplication gates, cross-platform builds, dependency advisories and the
   secret-scanner setup. Audit every new edit/capture API for native permission
   admission; updating the inventory alone must not grant missing authorization.
2. **Choose the billing and customer-auth integrations.** Preserve one-time and
   subscription offers. Select a supported billing provider and verified email
   identity/delivery flow. Keep Looksy account IDs independent of billing-provider
   IDs. Purchase email alone must not authenticate a customer or grant access.
3. **Build the missing account and VIP workflows.** Add email sign-in/recovery,
   secure desktop sessions, verified purchase linking, and admin-only VIP grants.
   Preserve guest-trial history, device limits and previous receipt-only rights.
   VIP grants are customer entitlements; they never confer staff authority.
4. **Configure isolated staging.** Use separate customer databases, signing
   keys, payment sandbox credentials, issuer/domain and update feeds from
   production. Enroll owner/support test subjects through authenticated staff
   setup. The hosted customer database remains separate from local photo catalogs.
   See [D1 environment separation](https://developers.cloudflare.com/d1/configuration/environments/)
   and [the current setup guide](customer-platform-setup.md).
5. **Finish distribution and diagnostics configuration.** Complete Apple
   Developer enrollment, Developer ID Application signing and notarization;
   configure Sparkle keys/feed and crash reporting. Windows needs its own
   signing/runtime configuration and native installed-package proof.
   [Apple enrollment](https://developer.apple.com/help/account/membership/enrolling-in-the-app),
   [Developer ID](https://developer.apple.com/developer-id/).
6. **Build a fresh managed test installer.** Include the staging API, public
   entitlement verification keys, reviewed checkout/portal links, crash handler,
   matching symbols and private update feed. The older local DMG in the release
   audit predates final fixes and has account/crash/update services disabled;
   it cannot be used as evidence for this journey.
7. **Run the installed-customer walkthrough below.** Record exact source commit,
   installer checksum, OS, backend environment, test identities, observed result
   and any backend incident/audit/event IDs. Use synthetic customer data and a
   disposable photo catalog. Preserve the daily editor and real customer data.
8. **Promote only after acceptance.** Repeat on Windows, verify two-version
   updates and restore, then configure production resources and reviewed prices,
   policies and support operations. Staging passes are not production fulfillment.

### Owner setup required

- [ ] Apple Developer membership, stable legal publisher, Developer ID certificate
  and notarization credentials.
- [ ] Cloudflare/domain access, staging D1/Worker and staff Access application/MFA.
- [ ] Selected customer email-auth provider and working verification/recovery
  delivery for the sender domain.
- [ ] Selected billing provider account, sandbox products for both offers,
  webhook credentials and customer portal. Determine whether an existing Lemon
  Squeezy store/customer migration is needed before replacing its adapter.
- [ ] Crash-reporting project and symbol-upload access; private update/download
  hosting and backed-up signing keys.
- [ ] VIP policy: included features, expiry, device/redemption limits and major
  version coverage. `beta` is currently reserved; updater eligibility follows
  build channel, so VIP/beta-update eligibility requires separate implementation
  if it is part of the offer.

### Installed-customer acceptance matrix

Use a clean Mac or isolated test user/catalog and separate browser identities
for ordinary customer, support and owner/admin. The owner's paid/VIP customer
account and staff dashboard identity must be tested independently.

| Done | Flow | Required observed result |
| --- | --- | --- |
| [ ] | Browser download and install | Fresh signed/notarized installer launches normally, identifies its version/channel, and needs no developer toolchain |
| [ ] | Create account and sign in | Verify email ownership; unverified, expired/replayed links or wrong credentials cannot authenticate; restart preserves the intended session |
| [ ] | Sign out and recover | Sign-out removes usable local credentials; recovery restores the correct account, rotates/revokes sessions as designed, and never resets an expired trial |
| [ ] | Guest to account | Account linking preserves the existing trial remainder and documents; signing in alone does not grant paid/VIP access |
| [ ] | Owner/admin console | Owner uses MFA; ordinary customers, spoofed headers, cross-account IDs and unenrolled staff cannot call admin APIs directly |
| [ ] | Support boundaries | Support can perform only approved actions; a removed test staff role loses access; support cannot mint VIP codes or escalate itself |
| [ ] | Create a VIP code | Owner chooses features, expiry, version and device/use limits; server issues a high-entropy code and records actor/time/reason; raw code is not logged |
| [ ] | Redeem and misuse VIP code | Customer receives the intended signed entitlement; invalid, expired, revoked, over-limit and concurrent duplicate redemption attempts are handled correctly |
| [ ] | Revoke and reissue | New redemption is denied; affected devices lose access according to the published refresh/offline policy; reissue never reactivates the old code; unrelated customers retain access |
| [ ] | Paid checkout | Both one-time and subscription sandbox purchases deliver access to the correct verified account; a redirect/success page alone cannot unlock the app |
| [ ] | Renewal, cancellation and refund | Verified provider events apply once; retry/failed-payment/canceled-but-paid-through/refund/chargeback states follow the reviewed policy |
| [ ] | Webhook reliability | Reject forged signatures; duplicate/out-of-order events and provider outages do not grant or resurrect access; failed receipts can be inspected and safely retried |
| [ ] | Billing management | Customer can open its own hosted portal, manage the subscription and recover a purchase; another customer's objects remain inaccessible |
| [ ] | Device permissions | Activate a second test device; enforce the cap under concurrency; revoke/deactivate and recover through actual Keychain/Credential Manager storage |
| [ ] | Editing admission | Access without an active trial, paid license or VIP grant cannot start protected edits or AI through UI, native calls or automation; existing work can still open, save and export; admitted operations finish |
| [ ] | Offline and service outage | Original signed expiry remains bounded; failed requests do not extend it; a failed replacement key preserves existing valid access; use isolated test clocks for expiry cases |
| [ ] | Files and OS permissions | Denied folders, read-only/missing drives and secure-store denial produce actionable errors without losing work or valid credentials |
| [ ] | Support logs | Trigger a controlled export/network failure; create/review a support bundle; match its incident ID to server logs without recording passwords, raw access codes or photo content |
| [ ] | Crash capture | In a disposable candidate process, generate a controlled crash; confirm the exact release and readable symbols; verify consent-off/no-upload and consent revocation, plus separate helper/GPU failure handling |
| [ ] | Real app update | Download/install version A, offer signed version B through the private feed, update/relaunch and retain account, license, documents and catalog |
| [ ] | Update failure and recovery | Reject tampered/wrong-signature/wrong-channel/ineligible-major updates; handle unavailable feed, interruption and full disk; rehearse compatible backup restore or corrected higher-build recovery |
| [ ] | Windows parity | Repeat install, account/device secure storage, permissions, crash reporting, checkout and App Installer A-to-B update on a clean supported Windows machine |

Revocation is not instantaneous while a device is offline. Specify and test an
acceptable online refresh/revocation target; shorten polling/lease policy if the
current six-hour refresh and seven-day paid offline allowance do not meet it.
Do not claim VIP-specific update access while the reserved beta claim is unused.

Keep matching dSYMs/PDBs for shipped executables and helpers. Native crash dumps
can contain process memory; consent and reviewed support-bundle sharing remain
part of acceptance. Sparkle update archives require cryptographic signatures.
[Sentry Native setup](https://docs.sentry.io/platforms/native/),
[Sparkle publishing](https://sparkle-project.org/documentation/publishing/).

### Pushed CI snapshot

The user pushed `7fae62e2` to `main`; the earlier PAT/push blocker is resolved.
At the 2026-11-17 check, [CI](https://github.com/bradyreiner/photo-editor/actions/runs/44161557484)
has repository hygiene passing, Windows desktop still running, Windows headless
failing with missing Metal backend symbols, and the Mac jobs cancelled. Other
native/source jobs are not green. A cancelled job is not platform qualification.

The [customer job](https://github.com/bradyreiner/photo-editor/actions/runs/44161557686)
reaches its admission audit and fails on unreviewed APIs and missing/delegated
guards, including timeline color-grade automation and Gaussian capture.
The [security run](https://github.com/bradyreiner/photo-editor/actions/runs/44161557434)
reports Rust dependency vulnerabilities and a secret-scanner action license/API
setup failure. Customer dependency installation also reports JavaScript
advisories. Review and remediate the findings; do not suppress a failing guard or
audit merely to obtain a green badge.

## Implemented in the repository

- [x] Public product identity is centralized as `Looksy`.
- [x] Stable default macOS bundle identifier is `com.virel.studio`.
- [x] macOS and Windows packages use production icon assets.
- [~] macOS packages include an app icon, legal notice, Rust helpers, Qt, and the managed
  PostgreSQL runtime; a current clean packaged-runtime proof remains required.
- [x] Windows packages use committed production visual assets rather than generated placeholders.
- [x] Windows `.appinstaller` metadata checks for updates on launch.
- [x] Sparkle is wired with a public EdDSA key and an HTTPS appcast URL.
- [x] Public releases fail if required identity, updater, website, publisher, or payment configuration is absent.
- [x] Tagged builds run Rust tests and the headless native test suite before packaging.
- [x] macOS release jobs import an ephemeral Developer ID certificate, sign with Hardened Runtime, notarize, staple, and verify selected architectures.
- [x] Windows release jobs sign, timestamp, and verify the MSIX signature.
- [x] Sparkle appcasts are generated from the selected notarized DMGs using a private key provided through CI standard input.
- [x] Public release promotion includes checksums, a release manifest, legal notices, and locked Rust dependency inventory.
- [x] Public artifacts are promoted to a separate public GitHub release repository; the private source repository is not exposed.
- [x] The website resolves the latest Apple-silicon, Intel, and Windows assets from the public release repository.
- [x] The website includes support/payment links plus privacy, terms, refund, license, and support pages.
- [x] Website deployment is automated through the existing Cloudflare Pages path after successful release promotion.
- [x] Production builds retain private local diagnostics and create an explicit review-before-sharing support bundle.
- [~] PostgreSQL pre-migration backups use checked custom-format `pg_dump` output; full restore and
  clean-machine recovery proof remain required.

## External launch blockers

- [ ] Complete trademark/name clearance for `Looksy` and approve the permanent legal publisher name.
- [ ] Own and configure the production website domain used by `LOOKSY_WEBSITE_URL`.
- [ ] Enroll the legal publisher in the Apple Developer Program.
- [ ] Export the Developer ID Application certificate as a protected P12 and configure the CI secrets.
- [ ] Create and protect the Apple notarization app-specific password.
- [ ] Obtain a publicly trusted Windows code-signing certificate and set the package publisher to its exact subject.
- [ ] Create the Sparkle EdDSA keypair, back up the private key offline, and configure public/private CI values.
- [ ] Create a public binary-only GitHub repository and a least-privilege token that can publish releases there.
- [ ] Create the Cloudflare Pages project, production domain, API token, and account variables.
- [ ] Select the replacement billing provider and test both offers, fulfillment, checkout, portal and recovery. Preserve any existing Lemon Squeezy purchases during migration.
- [ ] Configure the real support email and an operating process for refunds and security reports.
- [ ] Have qualified counsel review the evaluation license, privacy policy, terms, refund policy, Qt distribution model, and tax posture.
- [ ] Confirm every bundled AI model, codec, camera profile, font, image, and dependency is redistributable.

## Required clean-machine proof

- [ ] Install the Apple-silicon DMG on a clean supported Mac.
- [ ] When Intel is selected, install its DMG on a clean supported Intel Mac.
- [ ] Install `Looksy.appinstaller` on a clean Windows 10 22H2 or Windows 11 x64 machine.
- [ ] Confirm first launch works without Homebrew, Qt, Rust, PostgreSQL, Visual Studio, or other developer tools.
- [ ] Confirm managed PostgreSQL catalog creation, import, cull, edit, mask, export, quit, and relaunch.
- [ ] Confirm uninstall/reinstall preserves user data unless the user explicitly removes it.
- [ ] Publish a private `0.1.0` candidate and prove Sparkle updates it to `0.1.1` on every selected Mac architecture.
- [ ] Prove App Installer updates `0.1.0.0` to `0.1.1.0` without changing package identity.
- [ ] Confirm a pre-upgrade PostgreSQL custom-format backup is created, validated, and restored.
- [ ] Test missing drive, full disk, denied folder permission, interrupted export, corrupted cache, and unavailable update feed.
- [ ] Confirm every website download hash matches the promoted release manifest.

## Required production repository configuration

### Secrets

- `APPLE_DEVELOPER_ID_P12_BASE64`
- `APPLE_DEVELOPER_ID_P12_PASSWORD`
- `APPLE_DEVELOPER_ID_APP`
- `APPLE_NOTARY_APPLE_ID`
- `APPLE_NOTARY_TEAM_ID`
- `APPLE_NOTARY_PASSWORD`
- `SPARKLE_ED_PRIVATE_KEY_BASE64`
- `WINDOWS_SIGNING_CERT_BASE64`
- `WINDOWS_SIGNING_CERT_PASSWORD`
- `PUBLIC_RELEASES_TOKEN`
- `CLOUDFLARE_API_TOKEN`
- `CLOUDFLARE_ACCOUNT_ID`

### Variables

- `LOOKSY_PUBLIC_RELEASE_REPOSITORY`
- `LOOKSY_WEBSITE_URL`
- `LOOKSY_SUPPORT_EMAIL`
- `LOOKSY_LEGAL_PUBLISHER`
- `LOOKSY_SUPPORT_PAYMENT_URL`
- `LOOKSY_PURCHASE_URL` (required for paid releases)
- `LOOKSY_PRICE_LABEL`
- `LOOKSY_PURCHASE_RECOVERY_URL`
- `LOOKSY_COMMERCIAL_TERMS_APPROVED` (true only after review)
- `LOOKSY_SUPPORTED_PLATFORMS` (defaults to the committed product selection)
- `LOOKSY_CLOUDFLARE_PAGES_PROJECT`
- `PHOTO_EDITOR_MACOS_SPARKLE_ARCHIVE` or `PHOTO_EDITOR_MACOS_SPARKLE_URL`
- `PHOTO_EDITOR_MACOS_APPCAST_URL`
- `PHOTO_EDITOR_MACOS_SPARKLE_PUBLIC_KEY`
- `PHOTO_EDITOR_WINDOWS_PACKAGE_NAME`
- `PHOTO_EDITOR_WINDOWS_PACKAGE_DISPLAY_NAME`
- `PHOTO_EDITOR_WINDOWS_PACKAGE_PUBLISHER`
- `PHOTO_EDITOR_WINDOWS_PUBLISHER_DISPLAY_NAME`
- `PHOTO_EDITOR_WINDOWS_DESCRIPTION`
- `PHOTO_EDITOR_WINDOWS_TIMESTAMP_URL`
- `PHOTO_EDITOR_WINDOWS_APPINSTALLER_URI`
- `PHOTO_EDITOR_WINDOWS_PACKAGE_BASE_URI`

## Release sequence

1. Finish all clean-machine proof for the candidate.
2. Commit the version bump and release notes with a clean worktree.
3. Push tag `vX.Y.Z`.
4. Approve the protected `production` environment after reviewing the quality gate.
5. Confirm the selected notarized DMGs, signed MSIX, app installer, appcast, checksums, manifest, and notices are present in the public release.
6. Confirm Cloudflare Pages deployed the matching website configuration.
7. Install from the public page and perform a final smoke test.
8. Announce the release only after the updater feeds and support contact have been verified from an external network.
