(function () {
  const config = window.PhotoEditorDownloads || {};
  const state = {
    version: config.fallbackVersion || "0.1.0",
    macUrl: config.directDownloads?.mac || "",
    windowsUrl: config.directDownloads?.windows || "",
    notesUrl: config.releaseNotesUrl || ""
  };

  const elements = {
    macButton: document.getElementById("mac-download"),
    windowsButton: document.getElementById("windows-download"),
    macLabel: document.getElementById("mac-label"),
    windowsLabel: document.getElementById("windows-label"),
    status: document.getElementById("release-status"),
    version: document.getElementById("version-value"),
    notes: document.getElementById("release-notes")
  };

  function normalizePattern(pattern, fallback) {
    try {
      return new RegExp(pattern || fallback, "i");
    } catch (_error) {
      return new RegExp(fallback, "i");
    }
  }

  function formatVersion(value) {
    if (!value) {
      return state.version;
    }
    return value.replace(/^v/i, "");
  }

  function findAsset(assets, pattern) {
    return assets.find((asset) => pattern.test(asset.name || ""));
  }

  function enableDownload(anchor, label, url, labelText) {
    if (!url) {
      return;
    }

    anchor.href = url;
    anchor.classList.remove("disabled");
    anchor.removeAttribute("aria-disabled");
    label.textContent = labelText;
  }

  function render() {
    elements.version.textContent = `Version ${state.version}`;

    enableDownload(elements.macButton, elements.macLabel, state.macUrl, "Download DMG");
    enableDownload(
      elements.windowsButton,
      elements.windowsLabel,
      state.windowsUrl,
      "Download Windows"
    );

    if (state.notesUrl) {
      elements.notes.href = state.notesUrl;
      elements.notes.removeAttribute("aria-disabled");
      elements.notes.textContent = "View release notes";
    }

    if (state.macUrl || state.windowsUrl) {
      const available = [
        state.macUrl ? "macOS" : "",
        state.windowsUrl ? "Windows" : ""
      ].filter(Boolean);
      elements.status.textContent = `Latest build ready for ${available.join(" and ")}.`;
      return;
    }

    elements.status.textContent =
      "Downloads are not configured yet. Add release URLs in download-config.js.";
  }

  async function loadLatestRelease() {
    if (!config.githubOwner || !config.githubRepo) {
      render();
      return;
    }

    const apiUrl = `https://api.github.com/repos/${config.githubOwner}/${config.githubRepo}/releases/latest`;
    const response = await fetch(apiUrl, { headers: { Accept: "application/vnd.github+json" } });
    if (!response.ok) {
      throw new Error(`GitHub returned ${response.status}`);
    }

    const release = await response.json();
    const assets = Array.isArray(release.assets) ? release.assets : [];
    const macPattern = normalizePattern(config.macAssetPattern, "\\.(dmg|zip)$");
    const windowsPattern = normalizePattern(config.windowsAssetPattern, "\\.(msix|exe|msi)$");
    const macAsset = findAsset(assets, macPattern);
    const windowsAsset = findAsset(assets, windowsPattern);

    state.version = formatVersion(release.tag_name || release.name);
    state.notesUrl = config.releaseNotesUrl || release.html_url || state.notesUrl;
    state.macUrl = state.macUrl || macAsset?.browser_download_url || "";
    state.windowsUrl = state.windowsUrl || windowsAsset?.browser_download_url || "";
    render();
  }

  loadLatestRelease().catch(() => {
    elements.status.textContent =
      "Could not read the latest release. Check download-config.js or use direct URLs.";
    render();
  });
})();
