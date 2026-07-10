# Virel Studio Public Download Site

This is the static Cloudflare Pages surface for signed Virel Studio downloads,
optional support payments, release notes, product policies, and support.

## Configuration

Do not hand-edit production URLs into `download-config.js`. The release workflow
materializes it with:

```sh
python3 scripts/configure-release-site.py
python3 scripts/validate-release-site.py site
```

Relevant deployment values:

- `VIREL_PUBLIC_RELEASE_REPOSITORY` — public binary-only GitHub repository
- `VIREL_SUPPORT_EMAIL`
- `VIREL_SUPPORT_PAYMENT_URL` — hosted checkout/payment link
- `VIREL_PURCHASE_URL` — optional supporter-license checkout
- `VIREL_RELEASE_CHANNEL`
- `VIREL_ACCESS_MODE`

The source repository may remain private. Anonymous downloads come from the
separate public release repository.

## Commerce model

The public alpha uses `free-support`: installers are public and activation-free,
while a hosted payment link accepts optional support. No payment secret is
placed in the site. If mandatory paid access is introduced later, it needs a
separate webhook-backed license and fulfillment system; hiding a static URL is
not access control.

## Deployment

Tagged production releases deploy automatically after installers, update feeds,
checksums, and notices are promoted. Required Cloudflare values:

- secret `CLOUDFLARE_API_TOKEN`
- secret `CLOUDFLARE_ACCOUNT_ID`
- variable `VIREL_CLOUDFLARE_PAGES_PROJECT`

Cloudflare Pages hosts only the website. Large installers remain immutable
GitHub Release assets.

## Security and cache behavior

- `download-config.js` is served without caching so a release switch is immediate.
- Versioned visual assets are immutable.
- The Content Security Policy permits only same-origin resources and the public GitHub API.
- Payment happens on the configured provider's hosted page.
- `.appinstaller`, `.msix`, DMG, appcast, manifest, and checksums are released together.
