#!/usr/bin/env python3
import importlib.util
from pathlib import Path
import tempfile
import unittest
import xml.etree.ElementTree as ET

spec = importlib.util.spec_from_file_location('rollout', Path(__file__).with_name('configure-appcast-rollout.py'))
rollout = importlib.util.module_from_spec(spec)
spec.loader.exec_module(rollout)


class RolloutTests(unittest.TestCase):
    def test_signed_archive_survives_beta_stable_and_halt(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'appcast.xml'
            path.write_text(f'<rss xmlns:sparkle="{rollout.SPARKLE}"><channel><item><title>One</title>'
                '<pubDate>Wed, 23 Sep 2026 12:00:00 +0000</pubDate>'
                '<enclosure url="https://example.test/app.dmg" sparkle:shortVersionString="1.0" sparkle:edSignature="fixture" length="100" />'
                '</item></channel></rss>')
            original = dict(ET.parse(path).find('.//enclosure').attrib)
            rollout.configure(path, '1.0', 'beta')
            self.assertEqual(ET.parse(path).findtext(f'.//{{{rollout.SPARKLE}}}channel'), 'beta')
            self.assertEqual(ET.parse(path).find('.//enclosure').attrib, original)
            rollout.configure(path, '1.0')
            self.assertIsNone(ET.parse(path).find(f'.//{{{rollout.SPARKLE}}}channel'))
            self.assertEqual(ET.parse(path).findtext(f'.//{{{rollout.SPARKLE}}}phasedRolloutInterval'), '86400')
            before = path.read_bytes()
            with self.assertRaises(ValueError):
                rollout.configure(path, 'missing')
            self.assertEqual(path.read_bytes(), before)
            rollout.configure(path, '1.0', halt=True)
            self.assertIsNone(ET.parse(path).find('.//item'))


if __name__ == '__main__':
    unittest.main()
