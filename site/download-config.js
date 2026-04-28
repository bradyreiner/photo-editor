window.PhotoEditorDownloads = {
  productName: "PhotoEditor",
  channel: "Private alpha",
  fallbackVersion: "0.1.0",
  updatedAt: "",
  accessLabel: "Invited testers",

  // Private repo note:
  // GitHub Release downloads only work for users who have repository access.
  // For outside testers, use directDownloads until you publish a public release.

  // Option A: set these when the repository/release assets are public, or when
  // every tester has GitHub access to the private repository.
  githubOwner: "",
  githubRepo: "",

  // Match the release asset names produced by your packaging flow.
  macAssetPattern: "PhotoEditor.*\\.(dmg|zip)$",
  windowsAssetPattern: "PhotoEditor.*\\.(msix|exe|msi)$",

  // Option B: direct URLs for private MVP testing.
  directDownloads: {
    mac: "",
    windows: ""
  },

  labels: {
    mac: "DMG installer",
    windows: "Windows installer"
  },

  checksums: {
    mac: "",
    windows: ""
  },

  requirements: {
    mac: "macOS 14 or newer recommended.",
    windows: "Windows 10 22H2 or newer recommended."
  },

  releaseNotesUrl: "",
  supportEmail: "",
  accessNote:
    "This page can be public, but private GitHub Release assets only work for testers who have repository access. Use direct installer links while the repository stays private."
};
