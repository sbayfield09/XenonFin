#!/usr/bin/env python3
"""Native C HTTP/JSON integration against a disposable real Jellyfin server.
No video, personal server changes, or user credentials are involved.
"""
import json
import os
import pathlib
import secrets
import socket
import subprocess
import tempfile
import time
import urllib.error
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parents[1]

def main():
    binary = pathlib.Path(os.environ.get('JELLYFIN_TEST_SERVER', '/workspace/setup/jellyfin-server/jellyfin'))
    probe = ROOT / 'build/native-tests/probe'
    if not binary.is_file() or not probe.is_file():
        raise RuntimeError('Build make native-test and install the existing test server first')
    with tempfile.TemporaryDirectory(prefix='xenonfin-native-http-') as temporary:
        work = pathlib.Path(temporary)
        for name in ('data', 'config', 'cache', 'logs'):
            (work/name).mkdir()
        with socket.socket() as s:
            s.bind(('127.0.0.1', 0))
            port = s.getsockname()[1]
        base = f'http://127.0.0.1:{port}'
        (work/'config/network.xml').write_text(f'''<?xml version="1.0" encoding="utf-8"?>
<NetworkConfiguration><InternalHttpPort>{port}</InternalHttpPort><PublicHttpPort>{port}</PublicHttpPort><EnableRemoteAccess>false</EnableRemoteAccess><EnableIPv6>false</EnableIPv6><LocalNetworkAddresses><string>127.0.0.1</string></LocalNetworkAddresses><EnableUPnP>false</EnableUPnP></NetworkConfiguration>''')
        opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
        def api(path, body=None):
            data = None if body is None else json.dumps(body).encode()
            request = urllib.request.Request(base+path, data=data, headers={'Content-Type': 'application/json'})
            with opener.open(request, timeout=10) as response:
                raw = response.read()
                return json.loads(raw) if raw else None
        env = {**os.environ, 'NO_PROXY': '127.0.0.1,localhost', 'no_proxy': '127.0.0.1,localhost'}
        with (work/'server.log').open('wb') as log:
            process = subprocess.Popen([str(binary), '--datadir', str(work/'data'), '--configdir', str(work/'config'),
                '--cachedir', str(work/'cache'), '--logdir', str(work/'logs'), '--nowebclient', '--ffmpeg', '/usr/bin/ffmpeg', '--nonetchange'],
                env=env, stdout=log, stderr=subprocess.STDOUT)
            try:
                info = None
                for _ in range(120):
                    if process.poll() is not None:
                        raise RuntimeError('Temporary Jellyfin exited during startup')
                    try:
                        info = api('/System/Info/Public')
                        break
                    except (urllib.error.URLError, TimeoutError):
                        time.sleep(.25)
                if info is None:
                    raise RuntimeError('Temporary Jellyfin did not start')
                username = 'xenonfin-native-test'
                password = secrets.token_urlsafe(24) + '"\\'
                api('/Startup/Configuration', {'UICulture': 'en-US', 'MetadataCountryCode': 'GB', 'PreferredMetadataLanguage': 'en'})
                api('/Startup/User')
                api('/Startup/User', {'Name': username, 'Password': password})
                api('/Startup/RemoteAccess', {'EnableRemoteAccess': False, 'EnableAutomaticPortMapping': False})
                api('/Startup/Complete', {})
                for supplied, expected in ((password, 200), ('incorrect-test-password', 401)):
                    probe_env = {**env, 'JELLYFIN_USERNAME': username, 'JELLYFIN_PASSWORD': supplied}
                    result = subprocess.run([str(probe), base, str(expected)], env=probe_env, capture_output=True, text=True, timeout=25)
                    if result.returncode:
                        raise RuntimeError('Native C protocol integration failed: '+result.stderr.strip())
                    if supplied in result.stdout+result.stderr:
                        raise RuntimeError('Secret appeared in integration output')
                    print(result.stdout.strip(), flush=True)
                print('Real Jellyfin '+info['Version']+': native C GET, JSON escaping, login/token parsing and incorrect-password rejection PASS', flush=True)
            finally:
                process.terminate()
                try:
                    process.wait(timeout=15)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()

if __name__ == '__main__':
    main()
