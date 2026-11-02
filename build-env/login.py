"""Pebble appstore login for Podman/Docker on Windows.

Skips the localhost OAuth callback server (port publish is unreliable on
Windows containers). After you sign in via the printed URL, we poll the
broker's exchange API with the flow_id until credentials are ready.
"""
from __future__ import print_function

import argparse
import os
import subprocess
import sys
import time

import requests

DEFAULT_APPSTORE_API_BASE = os.getenv(
    'PEBBLE_APPSTORE_API_BASE', 'https://appstore-api.repebble.com'
).rstrip('/')
DEFAULT_OAUTH_BROKER_CLIENT_KEY = os.getenv(
    'PEBBLE_CLI_OAUTH_CLIENT_KEY', 'pebble-cli-public'
)


def _firebase_api_key():
    key = (os.getenv('PEBBLE_FIREBASE_API_KEY') or '').strip()
    if not key:
        sys.exit(
            'Set PEBBLE_FIREBASE_API_KEY (Firebase Web API key for the '
            'RePebble/Pebble Identity Toolkit project) before running login.'
        )
    return key


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--timeout', type=int, default=300,
                        help='Seconds to wait for browser sign-in (default 300)')
    args = parser.parse_args()

    broker = DEFAULT_APPSTORE_API_BASE
    client_key = DEFAULT_OAUTH_BROKER_CLIENT_KEY
    firebase_api_key = _firebase_api_key()
    # Dummy callback — nothing listens here. Browser will show a connection
    # error; that is OK. The broker still completes the flow; we poll exchange.
    # Avoid Chrome "unsafe ports" (e.g. 9); 8765 is fine.
    callback_url = 'http://127.0.0.1:8765/pebble-cli-oauth-done'

    start = requests.post(
        '%s/api/v1/cli-oauth/start' % broker,
        headers={'X-Pebble-Client-Key': client_key},
        json={'callback_url': callback_url},
        timeout=30,
    )
    try:
        start_payload = start.json()
    except ValueError:
        start_payload = {}
    if start.status_code >= 400:
        sys.exit('OAuth start failed (%s): %s' % (
            start.status_code, start_payload.get('error') or start.text[:200]))

    auth_url = start_payload.get('auth_url')
    flow_id = start_payload.get('flow_id')
    if not auth_url or not flow_id:
        sys.exit('OAuth start returned an invalid response.')

    print('Open this URL in your Windows browser and sign in:\n')
    print(auth_url)
    print('\nWhen the browser finishes (even if it shows a connection error),')
    print('come back here — waiting up to %d seconds…\n' % args.timeout)
    sys.stdout.flush()

    exchange_payload = None
    deadline = time.time() + args.timeout
    while time.time() < deadline:
        ex = requests.post(
            '%s/api/v1/cli-oauth/exchange' % broker,
            headers={'X-Pebble-Client-Key': client_key},
            json={'flow_id': flow_id},
            timeout=20,
        )
        try:
            exchange_payload = ex.json()
        except ValueError:
            exchange_payload = {}
        if ex.status_code >= 400:
            sys.exit('OAuth exchange failed (%s): %s' % (
                ex.status_code, exchange_payload.get('error') or ex.text[:200]))
        status = exchange_payload.get('status')
        if status == 'pending':
            time.sleep(1)
            continue
        if status == 'error':
            sys.exit('OAuth exchange error: %s' % (exchange_payload.get('error') or 'unknown'))
        if status == 'ok':
            break
        sys.exit('Unexpected OAuth exchange response: %s' % exchange_payload)
    else:
        sys.exit('Timed out waiting for sign-in. Run ./build.ps1 -Login again.')

    if exchange_payload.get('credential_type') != 'custom_token' or not exchange_payload.get('credential'):
        sys.exit('OAuth broker did not return a Firebase custom token.')

    print('Sign-in received. Exchanging Firebase token…')
    sys.stdout.flush()
    fb = requests.post(
        'https://identitytoolkit.googleapis.com/v1/accounts:signInWithCustomToken?key=%s'
        % firebase_api_key,
        json={'token': exchange_payload['credential'], 'returnSecureToken': True},
        timeout=30,
    )
    try:
        fb_payload = fb.json()
    except ValueError:
        fb_payload = {}
    if fb.status_code >= 400:
        sys.exit('Firebase sign-in failed: %s' % (
            (fb_payload.get('error') or {}).get('message') or fb.text[:200]))

    id_token = fb_payload.get('idToken')
    if not id_token:
        sys.exit('Firebase sign-in returned no idToken.')

    cmd = [
        'pebble', 'login',
        '--id-token', id_token,
        '--expires-in', str(int(fb_payload.get('expiresIn') or 3600)),
    ]
    if fb_payload.get('refreshToken'):
        cmd += ['--refresh-token', fb_payload['refreshToken']]
    if fb_payload.get('localId'):
        cmd += ['--local-id', fb_payload['localId']]

    print('Saving credentials…')
    sys.stdout.flush()
    rc = subprocess.call(cmd)
    if rc != 0:
        sys.exit(rc)

    subprocess.call(['pebble', 'login', '--status'])
    print('\nLogin OK. Next:\n  ./build.ps1 -Publish -ReleaseNotes "…"')


if __name__ == '__main__':
    main()
