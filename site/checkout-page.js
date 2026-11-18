(function () {
  "use strict";

  const status = document.getElementById("checkout-status");
  const retry = document.getElementById("checkout-retry");
  const sandbox = document.getElementById("checkout-environment");
  const config = window.LooksyPaddleCheckout || {};
  const failure = (message) => {
    status.textContent = message + " Open Looksy → Preferences → Account to start checkout again.";
    retry.hidden = true;
  };
  const page = new URL(window.location.href);
  const transactionId = page.searchParams.get("_ptxn") || "";
  // Never construct purchases from browser prices, customer IDs or account data.
  // Only the authenticated service creates the transaction and binds its owner.
  if (Array.from(page.searchParams.keys()).length !== 1 ||
      !/^txn_[a-z0-9]{26}$/.test(transactionId) || page.hash) {
    failure("This checkout link is missing or invalid.");
    return;
  }
  const expectedPrefix = config.environment === "sandbox" ? "test_" :
    config.environment === "production" ? "live_" : "";
  let origin;
  try { origin = new URL(config.checkoutOrigin); } catch (_) { /* invalid public configuration */ }
  if (!expectedPrefix || typeof config.clientToken !== "string" ||
      !new RegExp("^" + expectedPrefix + "[a-zA-Z0-9]{27}$").test(config.clientToken) ||
      !origin || origin.protocol !== "https:" || origin.username || origin.password ||
      origin.pathname !== "/" || origin.search || origin.hash ||
      page.protocol !== "https:" || page.origin !== origin.origin) {
    failure("Secure checkout is not configured for this website.");
    return;
  }
  sandbox.hidden = config.environment !== "sandbox";
  // Paddle.js automatically handles _ptxn. Remove it before loading the SDK so
  // this page opens exactly one checkout with the validated transaction below.
  try { window.history.replaceState(null, "", page.pathname); }
  catch (_) { failure("The checkout link could not be prepared safely."); return; }
  let initialized = false;
  let completed = false;
  const open = () => {
    if (!initialized || completed) return;
    try {
      retry.hidden = true;
      status.textContent = "Opening secure checkout…";
      window.Paddle.Checkout.open({ transactionId, settings: { allowLogout: false, theme: "dark" } });
    } catch (_) {
      status.textContent = "Checkout could not open. Try again, or return to Looksy → Preferences → Account.";
      retry.hidden = false;
    }
  };
  retry.addEventListener("click", open);
  const script = document.createElement("script");
  script.src = "https://cdn.paddle.com/paddle/v2/paddle.js";
  script.referrerPolicy = "no-referrer";
  script.onload = () => {
    try {
      const paddle = window.Paddle;
      if (!paddle || typeof paddle.Initialize !== "function" || typeof paddle.Checkout?.open !== "function")
        throw new Error("checkout SDK unavailable");
      if (config.environment === "sandbox") paddle.Environment.set("sandbox");
      paddle.Initialize({ token: config.clientToken, checkout: { settings: { allowLogout: false, theme: "dark" } },
        eventCallback: (event) => {
          if (event?.name === "checkout.completed") {
            completed = true;
            retry.hidden = true;
            status.textContent = "Checkout complete. Return to Looksy → Preferences → Account, refresh access, then select your license. Payment verification may take a moment.";
          } else if (!completed && event?.name === "checkout.closed") {
            status.textContent = "Checkout closed. You can reopen it or return to Looksy → Preferences → Account.";
            retry.hidden = false;
          } else if (!completed && (event?.name === "checkout.error" || event?.name === "checkout.payment.failed")) {
            status.textContent = "Payment could not be completed. Check your payment details in checkout or contact support.";
            retry.hidden = false;
          }
        }
      });
      initialized = true;
      open();
    } catch (_) { failure("Secure checkout could not start. Try again later."); }
  };
  script.onerror = () => failure("The payment service could not load. Check your connection and try again.");
  document.head.appendChild(script);
})();
