(function () {
  const publisher = window.LooksyDownloads?.legalPublisher || "";
  const fallback = "Publisher identity pending release approval";
  document.querySelectorAll("[data-legal-publisher]").forEach((element) => {
    element.textContent = publisher || fallback;
  });
})();
