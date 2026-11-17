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

The download landing page uses `landing.css` and the progressive Three.js
scene in `optical-scene.js`. Policy/support pages retain `styles.css`.
The app icon is the canonical logo; see `assets/README.md` for its source.

```sh
python3 -m http.server 4173 --bind 127.0.0.1 --directory site
node --test scripts/test-download-page.cjs
python3 scripts/validate-release-site.py site
```

Open `http://127.0.0.1:4173`. The R/G/B buttons toggle individual light rings,
and the motion button pauses/resumes the sculpture. Reduced-motion preferences
start it paused. Rendering stops while the hero is offscreen or the tab is hidden.
If WebGL or the Three.js module is unavailable, CSS artwork remains visible and
download resolution continues independently. All fonts and scripts are hosted
locally, preserving the existing Content Security Policy.
