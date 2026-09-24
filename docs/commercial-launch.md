# Looksy commercial launch

## Agreed product

- Both subscriptions and one-time licenses for the purchased major version.
- Apple silicon Mac first; Intel and Windows are deferred.
- Lemon Squeezy hosted checkout, receipts, and purchase recovery.
- Price is undecided. Legal publisher and domain are undecided.
- After expiry, viewing, saving and exporting existing work remain available;
  new edits require active access.

The customer-platform implementation and setup/evidence requirements are in
[the service runbook](../services/customer-platform/README.md). The receipt-only
notes below describe the previous release path, which remains the default desktop
configuration until the managed service is enabled. Both checkout URLs/prices and
revised terms are required before the new offer can be published.

`release/product.json` is the versioned product configuration. CI derives its
platform matrix, required artifacts and website availability from that selection.
The current implementation uses a receipt as proof of the purchased license,
without activation enforcement. Evaluation downloads remain public. One named
user may use the purchased major version on their own computers for personal
and client work. Maintenance updates released for that major version are included;
future major versions are separate purchases.

The commercial license and policy changes are drafts for review, not a completed
legal review. Paid deployment refuses to proceed until
`LOOKSY_COMMERCIAL_TERMS_APPROVED=true` is explicitly configured. It also requires
the price and checkout URL. The current version remains 0.1.0 alpha; the final
selling version and matching checkout description must be selected before sales.

## Local implementation and evidence

- Shared paid configuration validation in `scripts/release_commerce.py`.
- Hosted purchase, recovery, license and refund links on the existing website.
- Apple silicon selection in the website, CI matrix, promotion and appcast gates.
- Checkout stays disabled while price, checkout, recovery or legal approval is missing.
- Local candidate preparation in `scripts/prepare-sales-candidate.sh`.
- Local readiness report in `scripts/audit-launch-readiness.py`.
- Regression coverage: `scripts/test-release-commerce.py` and
  `scripts/test-download-page.cjs`; both run in CI.

Local engineering candidates are not signed production releases. They may use
uncommitted source and omit Sparkle until its production configuration exists.
The production packaging and publication gates still require signing, updater
configuration, a real legal publisher and a clean source revision.

```sh
# Create a new local engineering candidate and verify its packaged catalog.
bash scripts/prepare-sales-candidate.sh

# Inspect local prerequisites without exposing credential values.
python3 scripts/audit-launch-readiness.py --output build/launch-readiness.json

# Preview the website while checkout is still unconfigured.
python3 scripts/configure-release-site.py --preview
python3 -m http.server 8765 --bind 127.0.0.1 --directory site
```

The readiness report deliberately leaves external acceptance evidence open;
running this command cannot certify a clean customer machine, legal review,
photo quality, or payment-provider operations.

Current local results: [2026-09-23 evidence](evidence/commercial-launch-2026-09-23/README.md).

## Publisher and distribution setup

Choose the legal seller: your actual legal name or your registered business's
legal name. Looksy is the product name and does not by itself identify a legal
publisher. Confirm name availability and ownership before buying a domain or
registering accounts. Keep the existing app bundle identifier stable.

Use `release/production.env.example` as the non-secret configuration inventory.
For the first launch, configure the Apple credentials, Sparkle keys, public
binary release repository, Cloudflare Pages project/domain and support email
listed in `production-launch-checklist.md`. Windows credentials are not required
for the selected Apple silicon release.

Serve an update feed dedicated to the purchased major version. Do not repoint
that feed at a later paid major version. Verify the feed and download destinations
with a real two-version upgrade before selling. The updater feed hosting and
credentials remain external setup, not established by local build success.

## Lemon Squeezy setup and fulfillment

1. Create and activate the store under the chosen legal seller. Complete the
   provider's identity, payout and tax onboarding in the provider dashboard.
2. Create a single-payment Looksy product. Its title/description must name the
   purchased major version, Apple silicon support, one-user license, included
   maintenance updates, and separate future major upgrades. Choose a price and
   ensure `LOOKSY_PRICE_LABEL` agrees with checkout, including tax wording.
3. Attach the exact signed/notarized DMG, license, notices and checksums to the
   product's delivered files. Add official support and getting-started links.
   Each purchased major version needs its own product/variant delivery history;
   do not replace older buyers' files with a different paid major version.
4. Keep the provider's receipt/content-access flow enabled. Configure the
   receipt message to identify the purchased major version and recovery route.
   A support donation must not be described as a commercial license.
5. Put the hosted checkout URL in `LOOKSY_PURCHASE_URL`. Leave provider API
   secrets out of the desktop app and website. This receipt-based flow uses
   the provider's delivery; it does not need a custom webhook or license server.
6. Rehearse in test mode: successful payment, declined/cancelled payment,
   receipt arrival, download, My Orders recovery, and full refund. Then confirm
   one authorized live purchase/refund before opening general sales. Record the
   order identifier privately; do not commit customer or payment details.
7. For each maintenance release, update the delivered files for the same product
   and verify recovery still delivers the entitled major version. The repository
   does not currently upload files to Lemon Squeezy automatically.

Provider references: [products and delivered files](https://docs.lemonsqueezy.com/help/products/adding-products),
[My Orders](https://docs.lemonsqueezy.com/help/online-store/my-orders),
[hosted payments](https://docs.lemonsqueezy.com/guides/developer-guide/taking-payments).

## Acceptance before selling

Retain the exact commit, installer SHA-256, OS/hardware, camera/format, results and
screenshots for each run. Use a clean supported Mac with no development tools.

| Evidence | Acceptance |
| --- | --- |
| Install | Developer ID and notarization pass; app launches without Qt, Homebrew, Rust or PostgreSQL installed separately |
| Real project | Import RAW/JPEG, cull, edit, mask, undo, export; photographer approves preview/export appearance |
| Persistence | Quit/relaunch retains edits and catalog; uninstall/reinstall retains intended user data |
| Failure recovery | Interrupted export, missing drive, denied permission, full disk, corrupted cache and app crash recover without silent edit loss |
| Upgrade | Two signed versions update through Sparkle; pre-migration backup restores successfully |
| Scale/performance | Representative large library and long editing session remain usable on the minimum supported hardware |
| Purchase | Correct checkout price/rights, receipt, entitled downloads, recovery and refund all work |
| Support | Support address, diagnostics instructions, policies and known limitations are usable by an outside tester |
| Rights | Publisher/name review, commercial terms, Qt distribution and exact bundled dependency/model/font/profile rights are reviewed |

External beta participants must complete their own real project. Automation on
the development Mac and synthetic fixtures do not close this acceptance gate.
