# Looksy Public Download Site

This is the static Cloudflare Pages surface for signed Looksy downloads,
evaluation downloads, hosted purchases, release notes, product policies, and support.

## Configuration

Do not hand-edit production URLs into `download-config.js`. The release workflow
materializes it with:

```sh
python3 scripts/configure-release-site.py
python3 scripts/validate-release-site.py site
```

Relevant deployment values:

- `LOOKSY_PUBLIC_RELEASE_REPOSITORY` — public binary-only GitHub repository
- `LOOKSY_SUPPORT_EMAIL`
- `LOOKSY_LEGAL_PUBLISHER` — exact legal person or entity shown in policies and release metadata
- `LOOKSY_SUPPORT_PAYMENT_URL` — hosted checkout/payment link
- `LOOKSY_PURCHASE_URL` — optional supporter-license checkout
- `LOOKSY_RELEASE_CHANNEL`
- `LOOKSY_ACCESS_MODE`

The source repository may remain private. Anonymous downloads come from the
separate public release repository.

## Commerce model

The selected product is a one-time license for the purchased major version,
initially for Apple silicon Macs. `release/product.json` records these decisions.
Lemon Squeezy hosts checkout, receipts, and order recovery. The receipt is the
license entitlement; the app does not enforce activation or contact a license
server. Public downloads are available for evaluation. Public installer URLs
are intentional and are not treated as access control.

Set `LOOKSY_PURCHASE_URL`, `LOOKSY_PRICE_LABEL`, and the real publisher/support
configuration before deployment. Review the bundled LICENSE, site policies and
checkout rights, then set `LOOKSY_COMMERCIAL_TERMS_APPROVED=true`. Pricing stays
unset until the release owner chooses it. `LOOKSY_PURCHASE_RECOVERY_URL` defaults
to Lemon Squeezy My Orders. `LOOKSY_SUPPORTED_PLATFORMS` can override the committed
platform list, but every selected platform must pass its release gates.

The generator and public preflight reject incomplete paid settings. Use
`python3 scripts/configure-release-site.py --preview` only for local previews;
it keeps checkout disabled while configuration is incomplete. CI deployment
uses the strict generator and `validate-release-site.py site --production`.

See [commercial launch](../docs/commercial-launch.md) for the provider setup,
purchase delivery procedure and outstanding acceptance evidence.

## Deployment

Tagged production releases deploy automatically after installers, update feeds,
checksums, and notices are promoted. Required Cloudflare values:

- secret `CLOUDFLARE_API_TOKEN`
- secret `CLOUDFLARE_ACCOUNT_ID`
- variable `LOOKSY_CLOUDFLARE_PAGES_PROJECT`

Cloudflare Pages hosts only the website. Large installers remain immutable
GitHub Release assets.

## Security and cache behavior

- `download-config.js` is served without caching so a release switch is immediate.
- Versioned visual assets are immutable.
- The Content Security Policy permits only same-origin resources and the public GitHub API.
- Payment happens on the configured provider's hosted page.
- `.appinstaller`, `.msix`, DMG, appcast, manifest, and checksums are released together.

## Design and local preview

The landing page presents the current creative studio: photo/retouch, layered
design, boards, video/audio, Scene/3D Motion, and optional local Agent Studio.
`landing.css` controls the page; `product-tour.js` switches current app captures
with pointer and arrow/Home/End keyboard support. Policy pages retain `styles.css`.

```sh
python3 -m http.server 4173 --bind 127.0.0.1 --directory site
node --test scripts/test-download-page.cjs
python3 scripts/validate-release-site.py site
```

The screenshots under `assets/captures/2026-11-17/` were captured from the running
macOS app on that date. Each is an unretouched native window capture, with its
source, dimensions, and hash recorded beside it. Screenshots open at full size.
The gallery defaults to an actual saved multi-board design, and offers the
current photo editor and media library. Without scripting, all three remain
available in document order. Do not reuse historical UI fixtures as current UI.

Product copy is grounded in the current QML workspaces, controller interfaces,
and feature verification documents; the screenshot set does not qualify every
advertised workflow. Local models and model-dependent tools need setup, and
Agent availability depends on the build and configuration. Roadmap-only tools
are not presented as shipped capabilities.

The platform resolver must not overwrite `.lede`: platform availability belongs
in the download cards and system requirements. Increment the script URL revision
when changing it, so previously cached marketing copy cannot override the page.
Release and commerce controls retain the existing configuration-driven behavior.
