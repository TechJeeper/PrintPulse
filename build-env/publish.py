"""Publish PrintPulse to the Pebble appstore via `pebble publish`.

Uses store/listing.json for listing fields and store/screenshots/ for
platform-prefixed PNGs (emery_….png, basalt_….png, …). Icons are left to
the appstore generator unless store/icon-small.png and icon-large.png exist.

Usage (from the project root, inside the SDK container or with pebble on PATH):

  python3 build-env/publish.py
  python3 build-env/publish.py --release-notes "Fix ETA rounding"
  python3 build-env/publish.py --interactive   # prompts / uses pebble login session
"""
from __future__ import print_function

import argparse
import json
import os
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
LISTING = os.path.join(ROOT, 'store', 'listing.json')
SHOTS = os.path.join(ROOT, 'store', 'screenshots')
PKG = os.path.join(ROOT, 'package.json')


def load_json(path):
    with open(path, 'r', encoding='utf-8') as f:
        return json.load(f)


def screenshot_files():
    if not os.path.isdir(SHOTS):
        sys.exit('Missing %s — add platform-prefixed PNGs (e.g. emery_list.png).' % SHOTS)
    files = sorted(
        os.path.join(SHOTS, name)
        for name in os.listdir(SHOTS)
        if name.lower().endswith(('.png', '.jpg', '.jpeg', '.gif'))
    )
    if not files:
        sys.exit('No screenshots in %s' % SHOTS)
    return files


def main():
    parser = argparse.ArgumentParser(description='Publish PrintPulse to the Pebble appstore')
    parser.add_argument('--release-notes', default='', help='Release notes for this version')
    parser.add_argument('--interactive', action='store_true',
                        help='Allow pebble prompts (first listing / pebble login session)')
    args = parser.parse_args()

    listing = load_json(LISTING)
    version = load_json(PKG).get('version', '')
    shots = screenshot_files()

    cmd = ['pebble', 'publish', '--no-gif-all-platforms', '--screenshots'] + shots

    if args.release_notes:
        cmd += ['--release-notes', args.release_notes]
    if version:
        cmd += ['--version', version]

    if not args.interactive:
        cmd.append('--non-interactive')
        for flag, key in (
            ('--name', 'name'),
            ('--description', 'description'),
            ('--category', 'category'),
            ('--source', 'source'),
        ):
            if listing.get(key):
                cmd += [flag, listing[key]]
        # website is supported by the dashboard; pebble publish may ignore unknown flags,
        # so only pass fields documented for the tool.
        small = os.path.join(ROOT, 'store', 'icon-small.png')
        large = os.path.join(ROOT, 'store', 'icon-large.png')
        if os.path.isfile(small):
            cmd += ['--icon-small', small]
        if os.path.isfile(large):
            cmd += ['--icon-large', large]

    print('Running:', ' '.join(cmd[:6]), '... (%d screenshots)' % len(shots))
    env = os.environ.copy()
    # Prefer a CI token when present; otherwise pebble login session in ~/.pebble-sdk.
    rc = subprocess.call(cmd, cwd=ROOT, env=env)
    if rc != 0:
        sys.exit(rc)
    print('Published PrintPulse %s' % version)


if __name__ == '__main__':
    main()
