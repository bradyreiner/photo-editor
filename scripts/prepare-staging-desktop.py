#!/usr/bin/env python3
"""Prepare public desktop staging configuration, or validate downloaded update metadata.

This tool is offline: it neither creates credentials nor uploads/publishes files.
Signature presence checks are metadata checks; Sparkle/MSIX perform trust validation.
"""
import argparse
import base64
import json
import re
from pathlib import Path
from urllib.parse import unquote, urlsplit
import xml.etree.ElementTree as ET

SPARKLE = 'http://www.andymatuschak.org/xml-namespaces/sparkle'
APPINSTALLER = 'http://schemas.microsoft.com/appx/appinstaller/2021'
FIELDS = {'environment', 'customerApiUrl', 'entitlementPublicKey', 'entitlementKeyId',
          'policyMajor', 'sparkleFeedUrl', 'sparklePublicKey', 'downloadBaseUrl',
          'windowsAppInstallerUrl', 'sentryDsn'}


def https(value):
    if not isinstance(value, str) or any(ord(c) <= 32 or c in '\\";[]' for c in value):
        raise ValueError('Expected a public HTTPS URL without control characters')
    parsed = urlsplit(value)
    if parsed.scheme != 'https' or not parsed.hostname or parsed.username or parsed.password or parsed.query or parsed.fragment:
        raise ValueError('Expected an HTTPS URL without credentials, query or fragment')
    if parsed.hostname.endswith('.invalid'):
        raise ValueError('Replace example.invalid configuration before preparing a build')
    return parsed


def public_key(value):
    try:
        decoded = base64.b64decode(value, validate=True)
    except (TypeError, ValueError) as error:
        raise ValueError('Expected a base64 Ed25519 public key') from error
    if len(decoded) != 32 or not any(decoded):
        raise ValueError('Expected a nonzero 32-byte Ed25519 public key')
    return decoded


def resource_identity(field, value):
    if field in ('entitlementPublicKey', 'sparklePublicKey'):
        return public_key(value)
    url = urlsplit(value)
    # Hostnames and default HTTPS ports are case/representation insensitive.
    # Different public ingestion keys for the same Sentry project do not make
    # that project separate from production.
    return (url.hostname.lower() if url.hostname else '', url.port or 443,
            unquote(url.path).rstrip('/'))


def validate(config, production=None):
    if not isinstance(config, dict) or set(config) != FIELDS:
        raise ValueError('Configuration must contain exactly the documented public fields; secrets are not accepted')
    if not isinstance(config['sentryDsn'], str):
        raise ValueError('sentryDsn must be a public ingestion DSN string or empty')
    if config['environment'] != 'staging':
        raise ValueError('This tool only prepares staging builds')
    issuer = https(config['customerApiUrl'])
    if issuer.path not in ('', '/'):
        raise ValueError('Customer API must be an origin')
    for field in ('sparkleFeedUrl', 'downloadBaseUrl', 'windowsAppInstallerUrl'):
        https(config[field])
    public_key(config['entitlementPublicKey'])
    public_key(config['sparklePublicKey'])
    if public_key(config['entitlementPublicKey']) == public_key(config['sparklePublicKey']):
        raise ValueError('Entitlement and update signatures must use separate keys')
    key_id = config['entitlementKeyId']
    if not isinstance(key_id, str) or not key_id or len(key_id) > 80 or any(not(c.isascii() and (c.isalnum() or c in '_-')) for c in key_id):
        raise ValueError('Invalid public entitlement key ID')
    if type(config['policyMajor']) is not int or config['policyMajor'] < 0:
        raise ValueError('policyMajor must be a nonnegative integer')
    if config['sentryDsn']:
        # Sentry DSNs intentionally contain a public ingestion key as username.
        if not isinstance(config['sentryDsn'], str) or not re.fullmatch(r'https://[A-Za-z0-9]+@[A-Za-z0-9.-]+/[0-9]+', config['sentryDsn']):
            raise ValueError('Invalid public Sentry ingestion DSN')
        dsn = urlsplit(config['sentryDsn'])
        if dsn.scheme != 'https' or not dsn.hostname or not dsn.username or dsn.password or not dsn.path[1:].isdigit() or dsn.query or dsn.fragment:
            raise ValueError('Invalid public Sentry ingestion DSN')
    if production is not None:
        if not isinstance(production, dict):
            raise ValueError('Production comparison must be a public configuration object')
        # These identities/feeds must differ even if they share hosting vendors.
        for field in ('customerApiUrl', 'entitlementPublicKey', 'sparkleFeedUrl', 'sparklePublicKey', 'downloadBaseUrl', 'windowsAppInstallerUrl', 'sentryDsn'):
            stage = config[field]
            live = production.get(field)
            if stage and live and resource_identity(field, stage) == resource_identity(field, str(live)):
                raise ValueError(f'Staging must use a separate {field}')
    return config


def cmake(config):
    values = {
        'LOOKSY_CUSTOMER_API_URL': config['customerApiUrl'].rstrip('/'),
        'LOOKSY_ENTITLEMENT_PUBLIC_KEY': config['entitlementPublicKey'],
        'LOOKSY_ENTITLEMENT_KEY_ID': config['entitlementKeyId'],
        'LOOKSY_CUSTOMER_POLICY_MAJOR': str(config['policyMajor']),
        'LOOKSY_RELEASE_CHANNEL': 'staging',
        'LOOKSY_SENTRY_DSN': config['sentryDsn'],
        'LOOKSY_ENABLE_CRASH_REPORTING': 'ON' if config['sentryDsn'] else 'OFF',
        'PHOTO_EDITOR_ENABLE_MACOS_SPARKLE_UPDATER': 'ON',
        'PHOTO_EDITOR_MACOS_APPCAST_URL': config['sparkleFeedUrl'],
        'PHOTO_EDITOR_MACOS_SPARKLE_PUBLIC_KEY': config['sparklePublicKey'],
    }
    # Fixed bracket delimiter cannot be injected by these validated fields.
    return '# Public staging settings. No private keys or tokens.\n' + ''.join(
        f'set({key} [==[{value}]==] CACHE STRING "Staging" FORCE)\n' for key, value in values.items())


def in_download_base(url, base):
    parsed, prefix = https(url), https(base)
    path = unquote(parsed.path)
    return parsed.netloc == prefix.netloc and path.startswith(unquote(prefix.path).rstrip('/') + '/') and '..' not in path and '\\' not in path


def validate_feeds(config, appcast_path, appinstaller_path):
    rss = ET.parse(appcast_path).getroot()
    items = rss.findall('./channel/item')
    if not items:
        raise ValueError('Appcast must contain a signed staging release')
    for item in items:
        if item.findtext(f'{{{SPARKLE}}}channel') != 'staging':
            raise ValueError('Staging feed contains an untagged or different-channel release')
        enclosure = item.find('enclosure')
        if enclosure is None or not in_download_base(enclosure.get('url', ''), config['downloadBaseUrl']):
            raise ValueError('Appcast archive is outside the staging download base')
        try:
            signature = base64.b64decode(enclosure.get(f'{{{SPARKLE}}}edSignature', ''), validate=True)
        except ValueError as error:
            raise ValueError('Invalid Sparkle signature encoding') from error
        if len(signature) != 64 or not enclosure.get('length', '').isdigit() or int(enclosure.get('length', '0')) <= 0:
            raise ValueError('Appcast requires signature metadata and a positive archive length')
    installer = ET.parse(appinstaller_path).getroot()
    if installer.tag != f'{{{APPINSTALLER}}}AppInstaller' or installer.get('Uri') != config['windowsAppInstallerUrl']:
        raise ValueError('App Installer identity/URI does not match staging')
    package = installer.find(f'{{{APPINSTALLER}}}MainPackage')
    if package is None or package.get('Name') != 'Virel.Studio' or not package.get('Publisher') or not in_download_base(package.get('Uri', ''), config['downloadBaseUrl']):
        raise ValueError('App Installer must preserve the package identity and use staging archives')
    if installer.find(f'.//{{{APPINSTALLER}}}ForceUpdateFromAnyVersion') is not None:
        raise ValueError('Staging must not enable automatic downgrades')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('config', type=Path)
    parser.add_argument('--production-config', type=Path, help='Public configuration to check environment isolation')
    parser.add_argument('--output', type=Path, help='New CMake initial-cache file (refuses overwrite)')
    parser.add_argument('--appcast', type=Path)
    parser.add_argument('--appinstaller', type=Path)
    args = parser.parse_args()
    try:
        config = validate(json.loads(args.config.read_text()), json.loads(args.production_config.read_text()) if args.production_config else None)
        if bool(args.appcast) != bool(args.appinstaller):
            raise ValueError('Provide both --appcast and --appinstaller')
        if args.appcast:
            validate_feeds(config, args.appcast, args.appinstaller)
            print('Staging update metadata passed. Archive trust and installed A-to-B updates remain to be tested.')
        if args.output:
            with args.output.open('x') as output:
                output.write(cmake(config))
            print('Public staging cache prepared; SDK, signing and runtime packaging remain required.')
        if not args.production_config:
            print('Production isolation has not been compared: provide --production-config before promotion.')
    except (ValueError, OSError, ET.ParseError) as error:
        parser.exit(1, f'Staging configuration rejected: {error}\n')


if __name__ == '__main__':
    main()
