# PhotoEditor Download Site

This folder is a static Cloudflare Pages site for the PhotoEditor download page.

## Configure Downloads

Edit `download-config.js`.

For a public GitHub repository, or a private repository where every tester has
GitHub access, set:

```js
githubOwner: "your-github-user",
githubRepo: "photo-editor",
```

The page will read the latest GitHub Release and link matching release assets.
Adjust `macAssetPattern` and `windowsAssetPattern` if your packaged filenames do
not match the defaults.

For outside testers while the repository is private, leave the GitHub fields
blank and set direct URLs:

```js
directDownloads: {
  mac: "https://github.com/OWNER/REPO/releases/latest/download/PhotoEditor-0.1.0-Darwin.dmg",
  windows: "https://github.com/OWNER/REPO/releases/latest/download/PhotoEditor-0.1.0-x64.msix"
}
```

Private GitHub Release URLs still require GitHub access. If testers do not have
repository access, use a public release repo, a public object store link, or a
temporary file link for the installer binaries.

Optional fields worth filling before sharing:

```js
updatedAt: "2026-04-28",
checksums: {
  mac: "SHA256_WITHOUT_THE_SHA256_PREFIX",
  windows: "SHA256_WITHOUT_THE_SHA256_PREFIX"
},
releaseNotesUrl: "https://github.com/OWNER/REPO/releases/tag/v0.1.0",
supportEmail: "you@example.com"
```

## Deploy To Cloudflare Pages

1. Push this repo to GitHub.
2. In Cloudflare, create a Pages project from the GitHub repository.
3. Set the build command to blank.
4. Set the output directory to `site`.
5. Deploy.

Cloudflare will provide a free `*.pages.dev` URL. Add a custom domain later if
you want one.

## Release Asset Hosting

Do not upload the DMG or Windows installer into this folder. Cloudflare Pages
has a small per-file static asset limit. Use GitHub Releases for binaries and
let this page link to those release assets.
