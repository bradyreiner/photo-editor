"""Shared, public-only commerce configuration for release gates and the website."""

from __future__ import annotations

import os
import json
import base64
import re
from pathlib import Path
from urllib.parse import urlsplit


ACCESS_MODES = ("free-support", "paid-perpetual", "paid-both")
PLATFORMS = ("mac-arm", "mac-intel", "windows")


def validate_customer_build_config(get_value=None):
    get_value = get_value or os.environ.get
    origin = get_value("LOOKSY_CUSTOMER_API_URL", "")
    parsed = urlsplit(origin)
    if not https_url(origin) or parsed.path or parsed.query or parsed.fragment or ".invalid" in parsed.hostname:
        raise ValueError("LOOKSY_CUSTOMER_API_URL must be the configured production HTTPS origin")
    try:
        key = base64.b64decode(get_value("LOOKSY_ENTITLEMENT_PUBLIC_KEY", ""), validate=True)
    except (ValueError, TypeError) as error:
        raise ValueError("LOOKSY_ENTITLEMENT_PUBLIC_KEY must be a base64 Ed25519 public key") from error
    if len(key) != 32:
        raise ValueError("LOOKSY_ENTITLEMENT_PUBLIC_KEY must decode to 32 bytes")
    if not re.fullmatch(r"[A-Za-z0-9_-]{1,80}", get_value("LOOKSY_ENTITLEMENT_KEY_ID", "looksy-1")):
        raise ValueError("LOOKSY_ENTITLEMENT_KEY_ID is invalid")
    primary_id = get_value("LOOKSY_ENTITLEMENT_KEY_ID", "looksy-1")
    try:
        trusted = json.loads(get_value("LOOKSY_ENTITLEMENT_TRUSTED_KEYS_JSON", "{}"))
        if not isinstance(trusted, dict) or len(trusted) > 4:
            raise ValueError("invalid key count")
        for kid, encoded in trusted.items():
            if not re.fullmatch(r"[A-Za-z0-9_-]{1,80}", kid) or not isinstance(encoded, str):
                raise ValueError("invalid trusted key")
            decoded = base64.b64decode(encoded, validate=True)
            if len(decoded) != 32 or base64.b64encode(decoded).decode() != encoded:
                raise ValueError("invalid trusted key encoding")
            if kid == primary_id and decoded != key:
                raise ValueError("conflicting primary key")
    except (ValueError, TypeError) as error:
        raise ValueError("LOOKSY_ENTITLEMENT_TRUSTED_KEYS_JSON contains invalid or conflicting public keys") from error
    expected_major = get_value("LOOKSY_CUSTOMER_POLICY_MAJOR", "")
    app_major = (Path(__file__).resolve().parents[1] / "VERSION").read_text().strip().split(".")[0]
    if expected_major != app_major:
        raise ValueError("LOOKSY_CUSTOMER_POLICY_MAJOR is required and must match the released desktop major")


def https_url(value: str) -> bool:
    try:
        parsed = urlsplit(value)
        return (parsed.scheme == "https" and bool(parsed.hostname)
                and not parsed.username and not parsed.password
                and not any(character.isspace() for character in value))
    except ValueError:
        return False


def commerce_config(get_value=None, *, require_ready: bool = True) -> dict:
    get_value = get_value or (lambda name, default="": os.environ.get(name, "").strip() or default)
    product = json.loads((Path(__file__).resolve().parents[1] / "release/product.json").read_text())
    mode = get_value("LOOKSY_ACCESS_MODE", product["accessMode"])
    if mode not in ACCESS_MODES:
        raise ValueError("LOOKSY_ACCESS_MODE must be free-support, paid-perpetual or paid-both")
    raw_platforms = get_value("LOOKSY_SUPPORTED_PLATFORMS", ",".join(product["supportedPlatforms"]))
    platforms = [item.strip() for item in raw_platforms.split(",")]
    if (not platforms or len(set(platforms)) != len(platforms)
            or any(item not in PLATFORMS for item in platforms)):
        raise ValueError("LOOKSY_SUPPORTED_PLATFORMS must list unique mac-arm, mac-intel, windows values")
    config = {
        "accessMode": mode,
        "supportedPlatforms": platforms,
        "paymentProvider": "lemonsqueezy",
        "purchaseUrl": get_value("LOOKSY_PURCHASE_URL", product["purchaseUrl"]),
        "supportPaymentUrl": get_value("LOOKSY_SUPPORT_PAYMENT_URL"),
        "purchaseRecoveryUrl": get_value("LOOKSY_PURCHASE_RECOVERY_URL", product["purchaseRecoveryUrl"]),
        "priceLabel": get_value("LOOKSY_PRICE_LABEL", product["priceLabel"]),
        "subscriptionPurchaseUrl": get_value("LOOKSY_SUBSCRIPTION_PURCHASE_URL", product.get("subscriptionPurchaseUrl", "")),
        "subscriptionPriceLabel": get_value("LOOKSY_SUBSCRIPTION_PRICE_LABEL", product.get("subscriptionPriceLabel", "")),
        "billingPortalUrl": get_value("LOOKSY_BILLING_PORTAL_URL", product.get("billingPortalUrl", "")),
        "licenseSummary": get_value("LOOKSY_LICENSE_SUMMARY", product["licenseSummary"]),
        "commercialTermsApproved": get_value("LOOKSY_COMMERCIAL_TERMS_APPROVED", str(product["commercialTermsApproved"]).lower()) == "true",
    }
    validate_commerce(config, require_ready=require_ready)
    return config


def validate_commerce(config: dict, *, require_ready: bool) -> None:
    mode = config.get("accessMode")
    if mode not in ACCESS_MODES:
        raise ValueError("accessMode must be free-support, paid-perpetual or paid-both")
    platforms = config.get("supportedPlatforms")
    if (not isinstance(platforms, list) or not platforms
            or any(not isinstance(item, str) or item not in PLATFORMS for item in platforms)
            or len(set(platforms)) != len(platforms)):
        raise ValueError("supportedPlatforms must contain unique supported platform names")
    for field in ("purchaseUrl", "supportPaymentUrl", "purchaseRecoveryUrl", "priceLabel", "licenseSummary", "subscriptionPurchaseUrl", "subscriptionPriceLabel", "billingPortalUrl"):
        if not isinstance(config.get(field, ""), str):
            raise ValueError(f"{field} must be a string")
    for field in ("purchaseUrl", "supportPaymentUrl", "purchaseRecoveryUrl", "subscriptionPurchaseUrl", "billingPortalUrl"):
        if config.get(field) and not https_url(config[field]):
            raise ValueError(f"{field} must be an HTTPS URL without embedded credentials")
    if mode in ("paid-perpetual", "paid-both") and require_ready:
        required = {
            "LOOKSY_PURCHASE_URL": "purchaseUrl",
            "LOOKSY_PURCHASE_RECOVERY_URL": "purchaseRecoveryUrl",
            "LOOKSY_PRICE_LABEL": "priceLabel",
            "LOOKSY_LICENSE_SUMMARY": "licenseSummary",
        }
        if mode == "paid-both":
            required.update(LOOKSY_SUBSCRIPTION_PURCHASE_URL="subscriptionPurchaseUrl",
                            LOOKSY_SUBSCRIPTION_PRICE_LABEL="subscriptionPriceLabel",
                            LOOKSY_BILLING_PORTAL_URL="billingPortalUrl")
        missing = [name for name, field in required.items() if not config.get(field, "").strip()]
        if missing:
            raise ValueError("Paid release requires " + ", ".join(missing))
        if config.get("commercialTermsApproved") is not True:
            raise ValueError("Review LICENSE, terms, refunds, privacy and checkout rights, then set "
                             "LOOKSY_COMMERCIAL_TERMS_APPROVED=true")
