(function () {
  "use strict";

  const config = window.LooksyDownloads || {};
  const both = config.accessMode === "paid-both";
  const paid = both || config.accessMode === "paid-perpetual";
  const supportedPlatforms = config.supportedPlatforms || ["mac-arm", "mac-intel", "windows"];
  const supports = (platform) => supportedPlatforms.includes(platform);
  const httpsUrl = (value) => {
    try {
      const url = new URL(value);
      return url.protocol === "https:" && !url.username && !url.password;
    } catch (_) { return false; }
  };
  const purchaseReady = paid && config.commercialTermsApproved === true
    && config.priceLabel && config.licenseSummary && httpsUrl(config.purchaseUrl)
    && httpsUrl(config.purchaseRecoveryUrl)
    && (!both || (config.subscriptionPriceLabel && httpsUrl(config.subscriptionPurchaseUrl)));
  const state = {
    version: config.fallbackVersion || "0.1.0",
    updatedAt: config.updatedAt || "",
    notesUrl: config.releaseNotesUrl || "",
    macArmUrl: config.directDownloads?.macArm || "",
    macIntelUrl: config.directDownloads?.macIntel || "",
    windowsUrl: config.directDownloads?.windows || "",
    macArmLabel: "Signed and notarized DMG",
    macIntelLabel: "Signed and notarized DMG",
    windowsLabel: "Signed App Installer",
    errorMessage: ""
  };

  const elements = {
    body: document.body,
    channel: document.getElementById("channel-pill"),
    macArmButton: document.getElementById("mac-arm-download"),
    macIntelButton: document.getElementById("mac-intel-download"),
    windowsButton: document.getElementById("windows-download"),
    macArmLabel: document.getElementById("mac-arm-label"),
    macIntelLabel: document.getElementById("mac-intel-label"),
    windowsLabel: document.getElementById("windows-label"),
    status: document.getElementById("release-status"),
    version: document.getElementById("version-value"),
    updated: document.getElementById("updated-value"),
    access: document.getElementById("access-value"),
    notes: document.getElementById("release-notes"),
    support: document.getElementById("support-link"),
    macRequirement: document.getElementById("mac-requirement"),
    windowsRequirement: document.getElementById("windows-requirement"),
    supportPayment: document.getElementById("support-payment"),
    purchase: document.getElementById("purchase-button"),
    subscribe: document.getElementById("subscription-button"),
    recovery: document.getElementById("purchase-recovery"),
    commerceTitle: document.getElementById("commerce-title"),
    commerceDescription: document.getElementById("commerce-description"),
    commerceDetail: document.getElementById("commerce-detail")
  };

  function normalizePattern(pattern, fallback) {
    try {
      return new RegExp(pattern || fallback, "i");
    } catch (_error) {
      return new RegExp(fallback, "i");
    }
  }

  function formatDate(value) {
    if (!value) return "Not published";
    const date = new Date(value);
    if (Number.isNaN(date.getTime())) return value;
    return new Intl.DateTimeFormat(undefined, {
      month: "short",
      day: "numeric",
      year: "numeric"
    }).format(date);
  }

  function findAsset(assets, pattern) {
    return assets.find((asset) => pattern.test(asset.name || ""));
  }

  function enableLink(anchor, url, label, labelElement) {
    if (!anchor || !httpsUrl(url)) return;
    anchor.href = url;
    anchor.classList.remove("disabled");
    anchor.removeAttribute("aria-disabled");
    anchor.removeAttribute("tabindex");
    if (labelElement && label) labelElement.textContent = label;
  }

  function detectPlatform() {
    const platform = (navigator.userAgentData?.platform || navigator.platform || "").toLowerCase();
    if (platform.includes("win")) {
      elements.body.classList.add("platform-windows");
      elements.windowsButton?.classList.add("primary");
      elements.macArmButton?.classList.remove("primary");
      return;
    }
    // The browser's Mac platform string does not identify Intel vs Apple silicon.
  }

  function renderStaticConfig() {
    elements.channel.textContent = config.channel || "Public alpha";
    elements.access.textContent = config.accessLabel || "Public alpha";
    elements.macRequirement.textContent = config.requirements?.mac || elements.macRequirement.textContent;
    elements.windowsRequirement.textContent = config.requirements?.windows || elements.windowsRequirement.textContent;

    if (config.supportEmail) {
      elements.support.href = `mailto:${config.supportEmail}`;
      elements.support.textContent = config.supportEmail;
    }
    enableLink(elements.supportPayment, config.supportPaymentUrl, "Support development");
    if (paid) {
      document.getElementById("commerce-nav").textContent = "Buy";
      elements.commerceTitle.textContent = both ? "Choose how you create." : "Buy once. Keep creating.";
      elements.commerceDescription.textContent = config.licenseSummary || "Purchase details will be published before sales open.";
      elements.commerceDetail.textContent = both
        ? "Choose a subscription or a one-time license. When editing access ends, you can still view, save, and export existing work."
        : "Personal and client work. No subscription or periodic activation. Your receipt is your proof of purchase.";
      elements.purchase.textContent = purchaseReady ? `Buy Looksy — ${config.priceLabel}` : "Purchases opening soon";
      elements.supportPayment.hidden = true;
      if (purchaseReady) enableLink(elements.purchase, config.purchaseUrl);
      if (elements.subscribe) {
        elements.subscribe.hidden = !both;
        elements.subscribe.textContent = purchaseReady ? `Subscribe — ${config.subscriptionPriceLabel}` : "Subscriptions opening soon";
        if (purchaseReady && both) enableLink(elements.subscribe, config.subscriptionPurchaseUrl);
      }
      if (httpsUrl(config.purchaseRecoveryUrl)) {
        elements.recovery.hidden = false;
        enableLink(elements.recovery, config.purchaseRecoveryUrl);
      }
      document.querySelectorAll(".download-title").forEach((element) => {
        element.textContent = element.textContent.replace("Download", "Evaluate");
      });
    } else {
      if (elements.subscribe) elements.subscribe.hidden = true;
      // Support payments do not grant a commercial license.
      elements.purchase.hidden = true;
    }
    document.querySelectorAll("[data-platform-card]").forEach((element) => {
      element.hidden = !supports(element.dataset.platformCard);
    });
    document.getElementById("downloads").classList.toggle("single-platform", supportedPlatforms.length === 1);
    elements.windowsRequirement.hidden = !supports("windows");
    elements.macRequirement.hidden = !supports("mac-arm") && !supports("mac-intel");
    if (supports("mac-arm") && !supports("mac-intel")) {
      elements.macRequirement.textContent = "macOS 13 or newer on Apple silicon. Intel Macs are not supported by this release.";
    }
    if (supportedPlatforms.length === 1 && supports("mac-arm")) {
      document.querySelector(".lede").textContent = "Looksy is a native, local-first photo editor for organizing and shaping photography on Apple silicon Macs.";
    }
  }

  function render() {
    elements.version.textContent = state.version.replace(/^v/i, "");
    elements.updated.textContent = formatDate(state.updatedAt);
    if (supports("mac-arm")) enableLink(elements.macArmButton, state.macArmUrl, state.macArmLabel, elements.macArmLabel);
    if (supports("mac-intel")) enableLink(elements.macIntelButton, state.macIntelUrl, state.macIntelLabel, elements.macIntelLabel);
    if (supports("windows")) enableLink(elements.windowsButton, state.windowsUrl, state.windowsLabel, elements.windowsLabel);

    if (httpsUrl(state.notesUrl)) {
      elements.notes.href = state.notesUrl;
      elements.notes.removeAttribute("aria-disabled");
      elements.notes.textContent = "Read release notes and checksums";
    }

    const available = [
      supports("mac-arm") && httpsUrl(state.macArmUrl) ? "Apple silicon Mac" : "",
      supports("mac-intel") && httpsUrl(state.macIntelUrl) ? "Intel Mac" : "",
      supports("windows") && httpsUrl(state.windowsUrl) ? "Windows" : ""
    ].filter(Boolean);
    if (available.length) {
      elements.status.textContent = state.errorMessage || `Signed ${state.version} builds are ready for ${available.join(", ")}.`;
    } else {
      elements.status.textContent = state.errorMessage || "The first signed public build has not been published yet.";
    }
  }

  async function loadLatestRelease() {
    if (!config.githubOwner || !config.githubRepo) {
      render();
      return;
    }

    const response = await fetch(
      `https://api.github.com/repos/${config.githubOwner}/${config.githubRepo}/releases/latest`,
      { headers: { Accept: "application/vnd.github+json" } }
    );
    if (!response.ok) throw new Error(`GitHub returned ${response.status}`);

    const release = await response.json();
    const assets = Array.isArray(release.assets) ? release.assets : [];
    const patterns = config.assetPatterns || {};
    const macArm = findAsset(assets, normalizePattern(patterns.macArm, "Darwin-arm64.*\\.dmg$"));
    const macIntel = findAsset(assets, normalizePattern(patterns.macIntel, "Darwin-x86_64.*\\.dmg$"));
    const windowsInstaller = findAsset(assets, normalizePattern(patterns.windowsInstaller, "\\.appinstaller$"));
    const windowsPackage = findAsset(assets, normalizePattern(patterns.windows, "\\.msix$"));

    state.version = release.tag_name || release.name || state.version;
    state.updatedAt = release.published_at || release.created_at || state.updatedAt;
    state.notesUrl = config.releaseNotesUrl || release.html_url || state.notesUrl;
    state.macArmUrl = state.macArmUrl || macArm?.browser_download_url || "";
    state.macIntelUrl = state.macIntelUrl || macIntel?.browser_download_url || "";
    state.windowsUrl = state.windowsUrl || windowsInstaller?.browser_download_url || windowsPackage?.browser_download_url || "";
    state.macArmLabel = macArm?.name || state.macArmLabel;
    state.macIntelLabel = macIntel?.name || state.macIntelLabel;
    state.windowsLabel = windowsInstaller?.name || windowsPackage?.name || state.windowsLabel;
    render();
  }

  detectPlatform();
  renderStaticConfig();
  render();
  loadLatestRelease().catch(() => {
    state.errorMessage = "The release service could not be reached. Try again shortly or contact support.";
    render();
  });
})();
