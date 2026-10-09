#!/usr/bin/env python3
"""Regression tests for paid-release configuration and artifact selection."""

import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

from release_commerce import commerce_config, validate_commerce, validate_customer_build_config

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("release_platforms", ROOT / "scripts/release-platforms.py")
platforms = importlib.util.module_from_spec(spec)
spec.loader.exec_module(platforms)


def paid_environment():
    return {
        "LOOKSY_ACCESS_MODE": "paid-perpetual",
        "LOOKSY_SUPPORTED_PLATFORMS": "mac-arm",
        "LOOKSY_PURCHASE_URL": "https://fixture.lemonsqueezy.com/buy/example",
        "LOOKSY_PRICE_LABEL": "Fixture price",
        "LOOKSY_COMMERCIAL_TERMS_APPROVED": "true",
    }


class CommerceTests(unittest.TestCase):
    def test_cmake_checkout_and_rotation_configuration_rejects_unsafe_build_values(self):
        import base64
        source = (ROOT / "cmake/CustomerPlatform.cmake").read_text()
        validators = source.split('option(LOOKSY_ENABLE_CRASH_REPORTING', 1)[0]
        with tempfile.TemporaryDirectory() as folder:
            script = Path(folder) / "validate.cmake"
            script.write_text('set(PROJECT_VERSION_MAJOR 0)\n' + validators)
            def check(**values):
                return subprocess.run(['cmake', *(f'-D{name}={value}' for name, value in values.items()),
                                       '-P', str(script)], capture_output=True, text=True)
            good = check(LOOKSY_CUSTOMER_POLICY_MAJOR='0', LOOKSY_PURCHASE_URL='https://checkout.test/buy/one',
                         LOOKSY_SUBSCRIPTION_PURCHASE_URL='https://checkout.test/buy/monthly',
                         LOOKSY_BILLING_PORTAL_URL='https://checkout.test/billing')
            self.assertEqual(good.returncode, 0, good.stdout + good.stderr)
            for values in ({'LOOKSY_CUSTOMER_POLICY_MAJOR': '1'},
                           {'LOOKSY_BILLING_PORTAL_URL': 'https://user:password@checkout.test/billing'},
                           {'LOOKSY_PURCHASE_URL': 'http://checkout.test/buy'},
                           {'LOOKSY_ENTITLEMENT_TRUSTED_KEYS_JSON': '[]'},
                           {'LOOKSY_ENTITLEMENT_TRUSTED_KEYS_JSON': json.dumps({'looksy-1': base64.b64encode(bytes(32)).decode()})}):
                with self.subTest(values=values):
                    self.assertNotEqual(check(**values).returncode, 0)

    def test_managed_release_requires_a_real_origin_and_pinned_public_key(self):
        import base64
        values = {"LOOKSY_CUSTOMER_API_URL": "https://accounts.example.test",
                  "LOOKSY_ENTITLEMENT_PUBLIC_KEY": base64.b64encode(bytes(range(32))).decode(),
                  "LOOKSY_CUSTOMER_POLICY_MAJOR": (ROOT / "VERSION").read_text().strip().split(".")[0]}
        validate_customer_build_config(values.get)
        for override in ({"LOOKSY_CUSTOMER_API_URL": ""},
                         {"LOOKSY_CUSTOMER_API_URL": "https://accounts.example.invalid"},
                         {"LOOKSY_CUSTOMER_API_URL": "https://accounts.example.test/path"},
                         {"LOOKSY_ENTITLEMENT_PUBLIC_KEY": "bad"}):
            with self.subTest(override=override), self.assertRaises(ValueError):
                validate_customer_build_config(dict(values, **override).get)

    def config(self, values, **kwargs):
        return commerce_config(lambda name, default="": values.get(name, default), **kwargs)

    def test_paid_release_requires_checkout_and_price(self):
        with self.assertRaisesRegex(ValueError, "LOOKSY_PURCHASE_URL.*LOOKSY_PRICE_LABEL"):
            self.config({})

    def test_draft_is_explicit_and_does_not_approve_terms(self):
        config = self.config({}, require_ready=False)
        self.assertFalse(config["commercialTermsApproved"])
        self.assertEqual(config["purchaseUrl"], "")

    def test_paid_release_requires_legal_review(self):
        values = paid_environment()
        del values["LOOKSY_COMMERCIAL_TERMS_APPROVED"]
        with self.assertRaisesRegex(ValueError, "COMMERCIAL_TERMS_APPROVED"):
            self.config(values)

    def test_paid_release_does_not_require_support_payment(self):
        self.assertEqual(self.config(paid_environment())["supportPaymentUrl"], "")

    def test_both_purchase_models_require_both_checkout_prices(self):
        values = dict(paid_environment(), LOOKSY_ACCESS_MODE="paid-both")
        with self.assertRaisesRegex(ValueError, "LOOKSY_SUBSCRIPTION_PURCHASE_URL"):
            self.config(values)
        values.update(LOOKSY_SUBSCRIPTION_PURCHASE_URL="https://fixture.lemonsqueezy.com/buy/monthly",
                      LOOKSY_SUBSCRIPTION_PRICE_LABEL="Fixture monthly price")
        with self.assertRaisesRegex(ValueError, "LOOKSY_BILLING_PORTAL_URL"):
            self.config(values)
        values["LOOKSY_BILLING_PORTAL_URL"] = "https://fixture.lemonsqueezy.com/billing"
        self.assertEqual(self.config(values)["accessMode"], "paid-both")

    def test_rotation_keys_fail_closed_and_policy_matches_build_major(self):
        import base64
        values = {"LOOKSY_CUSTOMER_API_URL": "https://accounts.example.test",
                  "LOOKSY_ENTITLEMENT_PUBLIC_KEY": base64.b64encode(bytes(range(32))).decode(),
                  "LOOKSY_ENTITLEMENT_KEY_ID": "active",
                  "LOOKSY_CUSTOMER_POLICY_MAJOR": (ROOT / "VERSION").read_text().strip().split(".")[0]}
        old_key = base64.b64encode(bytes(range(31, -1, -1))).decode()
        validate_customer_build_config(dict(values, LOOKSY_ENTITLEMENT_TRUSTED_KEYS_JSON=json.dumps({"old": old_key})).get)
        for extra in ({"LOOKSY_CUSTOMER_POLICY_MAJOR": "999"},
                      {"LOOKSY_CUSTOMER_POLICY_MAJOR": ""},
                      {"LOOKSY_ENTITLEMENT_TRUSTED_KEYS_JSON": "[]"},
                      {"LOOKSY_ENTITLEMENT_TRUSTED_KEYS_JSON": json.dumps({"old": "bad"})},
                      {"LOOKSY_ENTITLEMENT_TRUSTED_KEYS_JSON": json.dumps({"active": old_key})}):
            with self.subTest(extra=extra), self.assertRaises(ValueError):
                validate_customer_build_config(dict(values, **extra).get)

    def test_invalid_links_rejected_even_in_preview(self):
        for value in ("javascript:alert(1)", "http://example.com", "https://secret@example.com", "https://bad host/a"):
            with self.subTest(value=value), self.assertRaises(ValueError):
                self.config({"LOOKSY_PURCHASE_URL": value}, require_ready=False)

    def test_unsupported_modes_and_platforms_rejected(self):
        for values in ({"LOOKSY_ACCESS_MODE": "subscription"}, {"LOOKSY_SUPPORTED_PLATFORMS": "linux"},
                       {"LOOKSY_SUPPORTED_PLATFORMS": "mac-arm,mac-arm"}, {"LOOKSY_SUPPORTED_PLATFORMS": ""}):
            with self.subTest(values=values), self.assertRaises(ValueError):
                self.config(values, require_ready=False)

    def test_approval_must_be_boolean_in_generated_config(self):
        config = self.config(paid_environment())
        config["commercialTermsApproved"] = "false"
        with self.assertRaises(ValueError):
            validate_commerce(config, require_ready=True)

    def test_source_platform_selection_is_apple_silicon(self):
        self.assertEqual(self.config({}, require_ready=False)["supportedPlatforms"], ["mac-arm"])

    def test_arm_only_artifacts_require_correct_architecture_and_appcast(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / "Looksy-0.1.0-Darwin-x86_64.dmg").write_text("fixture")
            self.assertEqual(len(platforms.validate_artifacts(root, ["mac-arm"], "0.1.0")), 2)
            (root / "Looksy-0.1.0-Darwin-arm64.dmg").write_text("fixture")
            (root / "appcast.xml").write_text("fixture")
            self.assertEqual(platforms.validate_artifacts(root, ["mac-arm"], "0.1.0"), [])

    def test_windows_quad_version_and_installer_both_required(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / "Looksy-0.1.0.0-x64.msix").write_text("fixture")
            self.assertTrue(platforms.validate_artifacts(root, ["windows"], "0.1.0"))
            (root / "Looksy.appinstaller").write_text("fixture")
            self.assertFalse(platforms.validate_artifacts(root, ["windows"], "0.1.0"))

    def test_generator_and_production_validator_agree(self):
        import shutil
        with tempfile.TemporaryDirectory() as folder, patch.dict(os.environ, paid_environment(), clear=True):
            root = Path(folder) / "site"
            shutil.copytree(ROOT / "site", root)
            os.environ.update(LOOKSY_LEGAL_PUBLISHER="Fixture Publisher", LOOKSY_SUPPORT_EMAIL="support@example.com")
            subprocess.run([sys.executable, str(ROOT / "scripts/configure-release-site.py"), "--output", str(root / "download-config.js")], check=True)
            subprocess.run([sys.executable, str(ROOT / "scripts/validate-release-site.py"), str(root), "--production"], check=True)
            contents = (root / "download-config.js").read_text()
            self.assertNotIn("COMMERCIAL_TERMS_APPROVED", contents)
            self.assertEqual(json.loads(contents.split("=", 1)[1].strip().removesuffix(";"))["accessLabel"], "One-time purchase")

    def test_mac_public_preflight_does_not_require_windows_or_donations(self):
        values = paid_environment()
        values.update(LOOKSY_LEGAL_PUBLISHER="Fixture Publisher", LOOKSY_SUPPORT_EMAIL="support@example.com",
                      LOOKSY_PUBLIC_RELEASE_REPOSITORY="fixture/releases", LOOKSY_WEBSITE_URL="https://example.com",
                      PHOTO_EDITOR_MACOS_APPCAST_URL="https://example.com/updates/v0/appcast.xml",
                      PHOTO_EDITOR_MACOS_SPARKLE_PUBLIC_KEY="fixture-public-key",
                      PHOTO_EDITOR_MACOS_SPARKLE_URL="https://example.com/Sparkle.tar.xz")
        with patch.dict(os.environ, values, clear=True):
            result = subprocess.run([sys.executable, str(ROOT / "scripts/release-preflight.py"),
                                     "--mode", "public", "--allow-dirty"], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
