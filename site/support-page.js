(function () {
  const email = window.LooksyDownloads?.supportEmail || "";
  const link = document.getElementById("support-email");
  if (link && email) {
    link.href = `mailto:${email}`;
    link.textContent = email;
    link.removeAttribute("aria-disabled");
  }
  const billing = document.getElementById("manage-subscription");
  try {
    const url = new URL(window.LooksyDownloads?.billingPortalUrl || "");
    if (billing && url.protocol === "https:" && !url.username && !url.password) {
      billing.href = url.href;
      billing.removeAttribute("aria-disabled");
    }
  } catch { /* The unconfigured release keeps subscription management disabled. */ }
})();
