#!/usr/bin/env python3
"""Set rollout policy on one signed Sparkle release without changing archives."""
import argparse
from email.utils import parsedate_to_datetime
from pathlib import Path
import xml.etree.ElementTree as ET

SPARKLE = 'http://www.andymatuschak.org/xml-namespaces/sparkle'
ET.register_namespace('sparkle', SPARKLE)


def configure(path, version, channel='stable', interval=86400, halt=False):
    if channel not in ('stable', 'beta') or not 0 <= interval <= 604800:
        raise ValueError('Invalid channel or rollout interval')
    tree = ET.parse(path)
    matches = []
    for parent in tree.getroot().iter('channel'):
        for item in parent.findall('item'):
            enclosure = item.find('enclosure')
            short = item.findtext(f'{{{SPARKLE}}}shortVersionString')
            if short is None and enclosure is not None:
                short = enclosure.get(f'{{{SPARKLE}}}shortVersionString')
            if short == version:
                matches.append((parent, item, enclosure))
    if len(matches) != 1:
        raise ValueError('Expected exactly one release with the requested short version')
    parent, item, enclosure = matches[0]
    if halt:
        parent.remove(item)
    else:
        if enclosure is None or not enclosure.get(f'{{{SPARKLE}}}edSignature') or not enclosure.get('url', '').startswith('https://'):
            raise ValueError('A signed HTTPS enclosure is required')
        if interval and not parsedate_to_datetime(item.findtext('pubDate', '')).tzinfo:
            raise ValueError('Phased rollout requires a timezone-aware publication date')
        for name in ('channel', 'phasedRolloutInterval'):
            for node in item.findall(f'{{{SPARKLE}}}{name}'):
                item.remove(node)
        if channel == 'beta':
            ET.SubElement(item, f'{{{SPARKLE}}}channel').text = 'beta'
        if interval:
            ET.SubElement(item, f'{{{SPARKLE}}}phasedRolloutInterval').text = str(interval)
    # Same-directory replacement leaves the original intact on validation error.
    temporary = Path(str(path) + '.tmp')
    tree.write(temporary, encoding='utf-8', xml_declaration=True)
    temporary.replace(path)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('appcast', type=Path)
    parser.add_argument('--version', required=True)
    parser.add_argument('--channel', choices=['stable', 'beta'], default='stable')
    parser.add_argument('--interval', type=int, default=86400)
    parser.add_argument('--halt', action='store_true')
    args = parser.parse_args()
    configure(args.appcast, args.version, args.channel, args.interval, args.halt)
