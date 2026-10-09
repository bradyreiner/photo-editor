#!/usr/bin/env python3
"""Keep compatibility data readers valid while rejecting obsolete public copy."""

import contextlib
import importlib.util
import io
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch


spec = importlib.util.spec_from_file_location(
    "brand_surface", Path(__file__).with_name("verify-brand-surface.py"))
brand = importlib.util.module_from_spec(spec)
spec.loader.exec_module(brand)


class BrandSurfaceTests(unittest.TestCase):
    def check(self, source):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            for name in brand.REQUIRED_FILES:
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.touch()
            (root / "CMakeLists.txt").write_text(
                'set(FRAMEHOUSE_PRODUCT_NAME "Looksy")\n'
                'set(FRAMEHOUSE_PRODUCT_SLUG "Looksy")\n'
                'set(FRAMEHOUSE_LEGAL_PUBLISHER "Fixture")\n')
            path = root / "apps/desktop/src/catalog_asset_model.cpp"
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(source)
            output = io.StringIO()
            with patch.object(brand, "ROOT", root), contextlib.redirect_stdout(output):
                result = brand.main()
            return result, output.getvalue()

    def test_old_data_directory_readers_are_compatible(self):
        statements = brand.ALLOWED_ENGINEERING_STATEMENTS[
            "apps/desktop/src/catalog_asset_model.cpp"]
        status, output = self.check("\n".join("  " + line for line in statements))
        self.assertEqual(status, 0, output)

    def test_displayed_engineering_name_is_rejected_in_same_file(self):
        status, output = self.check('setTitle(QStringLiteral("PhotoEditor"));')
        self.assertEqual(status, 1)
        self.assertIn("internal PhotoEditor identity leaked", output)

    def test_compatibility_statement_cannot_hide_public_copy(self):
        line = brand.ALLOWED_ENGINEERING_STATEMENTS[
            "apps/desktop/src/catalog_asset_model.cpp"][1]
        status, output = self.check(line + ' setTitle(QStringLiteral("PhotoEditor"));')
        self.assertEqual(status, 1)
        self.assertIn("internal PhotoEditor identity leaked", output)

    def test_obsolete_product_name_remains_rejected(self):
        status, output = self.check('setTitle(QStringLiteral("Stillfold"));')
        self.assertEqual(status, 1)
        self.assertIn("obsolete public brand", output)


if __name__ == "__main__":
    unittest.main()
