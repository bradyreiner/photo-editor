(function () {
  const config = window.PhotoEditorDownloads || {};
  const state = {
    version: config.fallbackVersion || "0.1.0",
    updatedAt: config.updatedAt || "",
    macUrl: config.directDownloads?.mac || "",
    windowsUrl: config.directDownloads?.windows || "",
    notesUrl: config.releaseNotesUrl || "",
    macLabel: config.labels?.mac || "DMG installer",
    windowsLabel: config.labels?.windows || "Windows installer",
    errorMessage: ""
  };

  const elements = {
    body: document.body,
    channel: document.getElementById("channel-pill"),
    macButton: document.getElementById("mac-download"),
    windowsButton: document.getElementById("windows-download"),
    macLabel: document.getElementById("mac-label"),
    windowsLabel: document.getElementById("windows-label"),
    macChecksum: document.getElementById("mac-checksum"),
    windowsChecksum: document.getElementById("windows-checksum"),
    status: document.getElementById("release-status"),
    version: document.getElementById("version-value"),
    updated: document.getElementById("updated-value"),
    access: document.getElementById("access-value"),
    channelValue: document.getElementById("channel-value"),
    notes: document.getElementById("release-notes"),
    support: document.getElementById("support-link"),
    macRequirement: document.getElementById("mac-requirement"),
    windowsRequirement: document.getElementById("windows-requirement"),
    accessNote: document.getElementById("access-note"),
    previewPanel: document.querySelector(".app-preview"),
    previewImage: document.getElementById("app-screenshot")
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

  function formatDate(value) {
    if (!value) {
      return "Not published";
    }

    const date = new Date(value);
    if (Number.isNaN(date.getTime())) {
      return value;
    }

    return new Intl.DateTimeFormat(undefined, {
      month: "short",
      day: "numeric",
      year: "numeric"
    }).format(date);
  }

  function findAsset(assets, pattern) {
    return assets.find((asset) => pattern.test(asset.name || ""));
  }

  function detectPlatform() {
    const platform = navigator.userAgentData?.platform || navigator.platform || "";
    const normalized = platform.toLowerCase();
    if (normalized.includes("mac")) {
      elements.body.classList.add("platform-mac");
      elements.macButton.classList.add("primary");
      elements.windowsButton.classList.remove("primary");
    }
    if (normalized.includes("win")) {
      elements.body.classList.add("platform-windows");
      elements.windowsButton.classList.add("primary");
      elements.macButton.classList.remove("primary");
    }
  }

  function enableDownload(anchor, label, url, labelText) {
    if (!url) {
      label.textContent = "Not configured yet";
      return;
    }

    anchor.href = url;
    anchor.classList.remove("disabled");
    anchor.removeAttribute("aria-disabled");
    anchor.removeAttribute("tabindex");
    label.textContent = labelText;
  }

  function setChecksum(element, value) {
    if (!value) {
      element.textContent = "";
      return;
    }
    element.textContent = `SHA256 ${value}`;
  }

  function renderStaticConfig() {
    elements.channel.textContent = config.channel || "Private alpha";
    elements.channelValue.textContent = config.channel || "Alpha";
    elements.access.textContent = config.accessLabel || "Invited testers";
    elements.macRequirement.textContent =
      config.requirements?.mac || "macOS 14 or newer recommended.";
    elements.windowsRequirement.textContent =
      config.requirements?.windows || "Windows 10 22H2 or newer recommended.";
    elements.accessNote.textContent = config.accessNote || elements.accessNote.textContent;

    if (config.supportEmail) {
      elements.support.href = `mailto:${config.supportEmail}`;
      elements.support.removeAttribute("aria-disabled");
      elements.support.textContent = config.supportEmail;
    }
  }

  function initializeScreenshotPreview() {
    if (!elements.previewPanel || !elements.previewImage) {
      return;
    }

    function showRealScreenshot() {
      elements.previewPanel.classList.add("has-real-screenshot");
    }

    function showCssPreview() {
      elements.previewPanel.classList.remove("has-real-screenshot");
      elements.previewImage.removeAttribute("src");
    }

    elements.previewImage.addEventListener("load", showRealScreenshot);
    elements.previewImage.addEventListener("error", showCssPreview);

    if (elements.previewImage.complete) {
      if (elements.previewImage.naturalWidth > 0) {
        showRealScreenshot();
      } else {
        showCssPreview();
      }
    }
  }

  function render() {
    elements.version.textContent = state.version;
    elements.updated.textContent = formatDate(state.updatedAt);

    enableDownload(elements.macButton, elements.macLabel, state.macUrl, state.macLabel);
    enableDownload(
      elements.windowsButton,
      elements.windowsLabel,
      state.windowsUrl,
      state.windowsLabel
    );
    setChecksum(elements.macChecksum, config.checksums?.mac);
    setChecksum(elements.windowsChecksum, config.checksums?.windows);

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
      elements.status.textContent =
        state.errorMessage || `Latest ${state.version} build ready for ${available.join(" and ")}.`;
      return;
    }

    elements.status.textContent =
      state.errorMessage ||
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
    state.updatedAt = release.published_at || release.created_at || state.updatedAt;
    state.notesUrl = config.releaseNotesUrl || release.html_url || state.notesUrl;
    state.macUrl = state.macUrl || macAsset?.browser_download_url || "";
    state.windowsUrl = state.windowsUrl || windowsAsset?.browser_download_url || "";
    state.macLabel = macAsset?.name || state.macLabel;
    state.windowsLabel = windowsAsset?.name || state.windowsLabel;
    render();
  }

  detectPlatform();
  renderStaticConfig();
  initializeScreenshotPreview();
  loadLatestRelease().catch(() => {
    state.errorMessage =
      "Could not read the latest release. Check download-config.js or use direct URLs.";
    render();
  });
})();
