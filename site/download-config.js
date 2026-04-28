window.PhotoEditorDownloads = {
  productName: "PhotoEditor",
  fallbackVersion: "0.1.0",

  // Option A: set these and the page will read the latest public GitHub release.
  // Example:
  // githubOwner: "your-github-user",
  // githubRepo: "photo-editor",
  githubOwner: "",
  githubRepo: "",

  // Match the release asset names produced by your packaging flow.
  macAssetPattern: "PhotoEditor.*\\.(dmg|zip)$",
  windowsAssetPattern: "PhotoEditor.*\\.(msix|exe|msi)$",

  // Option B: use direct URLs instead of the GitHub API.
  // These are useful before the repository is public or if filenames differ.
  directDownloads: {
    mac: "",
    windows: ""
  },

  releaseNotesUrl: ""
};
