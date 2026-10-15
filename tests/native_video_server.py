#!/usr/bin/env python3
"""Actual Jellyfin progressive transcode -> native C PS/decoder/scheduler/RGB.
Host only: final display is a probe, not Xbox GPU/hardware evidence. Disposable
account/server; never contacts or changes a personal Jellyfin installation.
"""
import argparse
import json
import os
from pathlib import Path
import secrets
import socket
import subprocess
import tempfile
import time
import urllib.error
import urllib.request

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--long', action='store_true', help='also decode a five-minute movie in real time on the HOST')
    args = parser.parse_args()
    binary = Path(os.environ.get('JELLYFIN_TEST_SERVER', '/workspace/setup/jellyfin-server/jellyfin'))
    probe = Path(os.environ.get('JELLYFIN_VIDEO_STREAM_PROBE', ROOT / 'build/native-tests/jvideo-probe'))
    ffmpeg = os.environ.get('FFMPEG', '/usr/bin/ffmpeg')
    if not binary.is_file() or not probe.is_file():
        raise RuntimeError('Install the pinned test server and run make native-test first')
    with tempfile.TemporaryDirectory(prefix='xenonfin-video-stream-') as directory:
        work = Path(directory)
        for name in ('data', 'config', 'cache', 'logs', 'movies', 'tv'):
            (work/name).mkdir()
        clip = work/'movies/Native Stream.mp4'
        subprocess.run([ffmpeg, '-hide_banner', '-loglevel', 'error', '-f', 'lavfi', '-i',
                        'testsrc2=s=640x360:r=24', '-f', 'lavfi', '-i', 'sine=frequency=440',
                        '-t', '8', '-c:v', 'libx264', '-preset', 'ultrafast', '-c:a', 'aac', str(clip)], check=True)
        season = work/'tv/Native Series/Season 01'
        season.mkdir(parents=True)
        # Same moving video, but real 5.1 AAC: MP2 output must be downmixed
        # even though the console discards audio packets for this stage.
        subprocess.run([ffmpeg, '-hide_banner', '-loglevel', 'error', '-i', str(clip),
                        '-c:v', 'copy', '-ac', '6', '-c:a', 'aac',
                        str(season/'Native Series S01E01.mp4')], check=True)
        (season/'Native Series S01E01.nfo').write_text('<episodedetails><title>Stream Episode</title><season>1</season><episode>1</episode></episodedetails>')
        if args.long:
            subprocess.run([ffmpeg, '-hide_banner', '-loglevel', 'error', '-stream_loop', '-1', '-i', str(clip),
                            '-t', '300', '-c', 'copy', str(work/'movies/Long Stream.mp4')], check=True)
        with socket.socket() as sock:
            sock.bind(('127.0.0.1', 0))
            port = sock.getsockname()[1]
        base = f'http://127.0.0.1:{port}'
        (work/'config/network.xml').write_text(f'<NetworkConfiguration><InternalHttpPort>{port}</InternalHttpPort><PublicHttpPort>{port}</PublicHttpPort><EnableRemoteAccess>false</EnableRemoteAccess><EnableIPv6>false</EnableIPv6><LocalNetworkAddresses><string>127.0.0.1</string></LocalNetworkAddresses><EnableUPnP>false</EnableUPnP></NetworkConfiguration>')
        opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
        token = ''

        def api(path, body=None):
            headers = {'Content-Type': 'application/json', 'Authorization': 'MediaBrowser Client="XenonFinTests", Device="Host", DeviceId="native-video", Version="0.6"'+(', Token="'+token+'"' if token else '')}
            request = urllib.request.Request(base+path, data=None if body is None else json.dumps(body).encode(), headers=headers)
            with opener.open(request, timeout=25) as response:
                data = response.read()
                return json.loads(data) if data else None

        env = {**os.environ, 'NO_PROXY': '127.0.0.1,localhost', 'no_proxy': '127.0.0.1,localhost'}
        with (work/'server.log').open('wb') as log:
            process = subprocess.Popen([str(binary), '--datadir', str(work/'data'), '--configdir', str(work/'config'),
                                        '--cachedir', str(work/'cache'), '--logdir', str(work/'logs'), '--nowebclient',
                                        '--ffmpeg', ffmpeg, '--nonetchange'], stdout=log, stderr=log, env=env)
            try:
                info = None
                for _ in range(120):
                    if process.poll() is not None:
                        raise RuntimeError('Temporary server exited during startup')
                    try:
                        info = api('/System/Info/Public')
                        break
                    except (urllib.error.URLError, TimeoutError):
                        time.sleep(.25)
                if info is None:
                    raise RuntimeError('Temporary server did not start')
                password = secrets.token_urlsafe(24)
                api('/Startup/Configuration', {'UICulture': 'en-US'})
                api('/Startup/User')
                api('/Startup/User', {'Name': 'video-test', 'Password': password})
                api('/Startup/RemoteAccess', {'EnableRemoteAccess': False, 'EnableAutomaticPortMapping': False})
                api('/Startup/Complete', {})
                auth = api('/Users/AuthenticateByName', {'Username': 'video-test', 'Pw': password})
                token, user = auth['AccessToken'], auth['User']['Id']
                for name, kind, folder in (('Movies', 'movies', 'movies'), ('TV', 'tvshows', 'tv')):
                    api('/Library/VirtualFolders?name='+name+'&collectionType='+kind+'&refreshLibrary=true',
                        {'LibraryOptions': {'PathInfos': [{'Path': str(work/folder)}], 'EnableRealtimeMonitor': False,
                                            'TypeOptions': [{'Type': t, 'MetadataFetchers': [], 'ImageFetchers': []} for t in ('Movie', 'Series', 'Season', 'Episode')]}})
                indexed = []
                for _ in range(240):
                    indexed = api('/Items?Recursive=true&IncludeItemTypes=Movie,Episode')['Items']
                    if len(indexed) >= (3 if args.long else 2):
                        break
                    time.sleep(.25)
                movie = next(i for i in indexed if i['Type'] == 'Movie' and i['Name'] == 'Native Stream')
                episode = next(i for i in indexed if i['Type'] == 'Episode')
                cases = [(movie, '', 'movie/realtime/pause/resume/end'), (episode, '', 'episode/realtime'), (movie, '--stop', 'cancel/report/encoding cleanup')]
                if args.long:
                    long_movie = next(i for i in indexed if i['Type'] == 'Movie' and i['Name'] == 'Long Stream')
                    cases.append((long_movie, '', 'five-minute media/realtime HOST clock'))
                for item, flag, label in cases:
                    command = [str(probe), base, item['Id']]+([flag] if flag else [])
                    result = subprocess.run(command, env={**env, 'JELLYFIN_ACCESS_TOKEN': token, 'JELLYFIN_USER_ID': user},
                                            capture_output=True, text=True, timeout=420)
                    if token in result.stdout+result.stderr or password in result.stdout+result.stderr:
                        raise RuntimeError('Secret appeared in probe output')
                    if result.returncode:
                        raise RuntimeError('Native video probe failed: '+result.stderr.strip())
                    metrics = json.loads(result.stdout)
                    if args.long and item.get('Name') == 'Long Stream':
                        assert metrics['decoded'] >= 8990 and metrics['received'] > 8*1024*1024, metrics
                    elif not flag or flag == '--accelerated':
                        assert metrics['decoded'] == 240, metrics
                    assert metrics['reports_204'] and metrics['cleaned'] and metrics['moving_rgb_frames'] > 20, metrics
                    print(label+': '+json.dumps(metrics), flush=True)
                print('Real Jellyfin '+info['Version']+' MPEG-1/MPEG-PS negotiation, incremental decode/RGB, pause/cancel/reports PASS; Xbox presentation and five-minute console stability PENDING', flush=True)
            finally:
                process.terminate()
                try:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()


if __name__ == '__main__':
    main()
