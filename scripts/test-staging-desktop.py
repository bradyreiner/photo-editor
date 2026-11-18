#!/usr/bin/env python3
import base64
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('staging', Path(__file__).with_name('prepare-staging-desktop.py'))
staging = importlib.util.module_from_spec(spec)
spec.loader.exec_module(staging)


def configuration():
    return dict(environment='staging', customerApiUrl='https://accounts.stage.test',
                entitlementPublicKey=base64.b64encode(bytes(range(32))).decode(),
                entitlementKeyId='stage-1', policyMajor=0,
                sparkleFeedUrl='https://updates.stage.test/appcast.xml',
                sparklePublicKey=base64.b64encode(bytes(range(1, 33))).decode(),
                downloadBaseUrl='https://updates.stage.test/artifacts/',
                windowsAppInstallerUrl='https://updates.stage.test/Looksy.appinstaller', sentryDsn='')


class StagingTests(unittest.TestCase):
    def test_public_cache_cannot_include_secrets_or_inject_cmake(self):
        good = staging.validate(configuration())
        self.assertIn('LOOKSY_RELEASE_CHANNEL [==[staging]==]', staging.cmake(good))
        for override in ({'privateKey': 'secret'}, {'customerApiUrl': 'https://user:secret@test'},
                         {'sparkleFeedUrl': 'https://updates.test/]==]'},
                         {'entitlementPublicKey': base64.b64encode(bytes(32)).decode()},
                         {'sparklePublicKey': good['entitlementPublicKey']},
                         {'customerApiUrl': 'https://accounts.example.invalid'},
                         {'environment': 'production'}, {'policyMajor': True},
                         {'sentryDsn': []},
                         {'sentryDsn': 'https://key@host.test/1\nset(OTHER ON)'}):
            with self.subTest(override=override), self.assertRaises(ValueError):
                staging.validate(dict(good, **override))

    def test_cache_generation_refuses_overwrite(self):
        with tempfile.TemporaryDirectory() as directory:
            config = Path(directory) / 'public.json'
            output = Path(directory) / 'staging.cmake'
            config.write_text(json.dumps(configuration()))
            command = [sys.executable, str(Path(__file__).with_name('prepare-staging-desktop.py')), str(config), '--output', str(output)]
            first = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(first.returncode, 0, first.stderr)
            before = output.read_bytes()
            self.assertNotEqual(subprocess.run(command, capture_output=True).returncode, 0)
            self.assertEqual(output.read_bytes(), before)

    def test_separate_production_resources_required_when_comparing(self):
        stage = configuration()
        for field in ('customerApiUrl', 'entitlementPublicKey', 'sparkleFeedUrl', 'sparklePublicKey', 'downloadBaseUrl', 'windowsAppInstallerUrl'):
            with self.subTest(field=field), self.assertRaises(ValueError):
                staging.validate(stage, {field: stage[field]})
        with self.assertRaises(ValueError):
            staging.validate(stage, {'customerApiUrl': 'https://ACCOUNTS.STAGE.TEST:443/'})
        stage['sentryDsn'] = 'https://stageKey@ingest.sentry.test/123'
        with self.assertRaises(ValueError):
            staging.validate(stage, {'sentryDsn': 'https://productionKey@ingest.sentry.test/123'})

    def test_metadata_rejects_channel_and_download_environment_mixing(self):
        config = configuration()
        signature = base64.b64encode(bytes(64)).decode()
        appcast = f'<rss xmlns:sparkle="{staging.SPARKLE}"><channel><item><sparkle:channel>staging</sparkle:channel><enclosure url="https://updates.stage.test/artifacts/Looksy.dmg" length="100" sparkle:edSignature="{signature}"/></item></channel></rss>'
        installer = f'<AppInstaller xmlns="{staging.APPINSTALLER}" Uri="{config["windowsAppInstallerUrl"]}"><MainPackage Name="Virel.Studio" Publisher="CN=Test" Uri="https://updates.stage.test/artifacts/Looksy.msix" /></AppInstaller>'
        with tempfile.TemporaryDirectory() as directory:
            mac, windows = Path(directory) / 'appcast.xml', Path(directory) / 'Looksy.appinstaller'
            mac.write_text(appcast)
            windows.write_text(installer)
            staging.validate_feeds(config, mac, windows)
            for changed in (appcast.replace('>staging<', '>beta<'), appcast.replace('<sparkle:channel>staging</sparkle:channel>', ''), appcast.replace('/artifacts/', '/production/'), appcast.replace(signature, 'invalid'), appcast.replace('length="100"', 'length="0"')):
                mac.write_text(changed)
                with self.assertRaises(ValueError):
                    staging.validate_feeds(config, mac, windows)
            mac.write_text(appcast)
            for changed in (installer.replace('Virel.Studio', 'New.Identity'), installer.replace('/artifacts/', '/prod/'), installer.replace('Looksy.appinstaller', 'Other.appinstaller'), installer.replace('</AppInstaller>', '<ForceUpdateFromAnyVersion>false</ForceUpdateFromAnyVersion></AppInstaller>')):
                windows.write_text(changed)
                with self.assertRaises(ValueError):
                    staging.validate_feeds(config, mac, windows)


if __name__ == '__main__':
    unittest.main()
