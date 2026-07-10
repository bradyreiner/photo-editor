(function () {
  const email = window.VirelDownloads?.supportEmail || "";
  const link = document.getElementById("support-email");
  if (!link || !email) return;
  link.href = `mailto:${email}`;
  link.textContent = email;
  link.removeAttribute("aria-disabled");
})();
