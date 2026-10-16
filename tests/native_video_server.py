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
    parser.add_argument('--long-seconds', type=int, default=300, choices=range(60, 601), metavar='60..600', help='host long-run duration; default 300, maximum 600 seconds')
    parser.add_argument('--extended', action='store_true', help='test video-only, delayed/short audio, long pause, network stall and slow device clock')
    parser.add_argument('--case', help='run only cases whose label contains this text')
    parser.add_argument('--metrics', type=Path, help='write safe structured host test evidence')
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
        # Same moving video, with real 5.1 AAC downmixed to decoded MP3 stereo.
        subprocess.run([ffmpeg, '-hide_banner', '-loglevel', 'error', '-i', str(clip),
                        '-c:v', 'copy', '-ac', '6', '-c:a', 'aac',
                        str(season/'Native Series S01E01.mp4')], check=True)
        (season/'Native Series S01E01.nfo').write_text('<episodedetails><title>Stream Episode</title><season>1</season><episode>1</episode></episodedetails>')
        sync_clip = work/'movies/Native Sync.mp4'
        subprocess.run([ffmpeg, '-hide_banner', '-loglevel', 'error', '-f', 'lavfi', '-i',
                        "color=c=black:s=640x360:r=30,drawbox=color=white:t=fill:enable='lt(mod(t,1),0.1)'",
                        '-f', 'lavfi', '-i', "aevalsrc='if(lt(mod(t,1),0.05),0.4*sin(2*PI*1000*t),0)':s=48000",
                        '-t', '8', '-c:v', 'libx264', '-preset', 'ultrafast', '-pix_fmt', 'yuv420p',
                        '-c:a', 'aac', str(sync_clip)], check=True)
        if args.extended:
            subprocess.run([ffmpeg, '-hide_banner', '-loglevel', 'error', '-stream_loop', '1', '-i', str(clip), '-t', '16', '-c', 'copy', str(work/'movies/Gapped Audio.mp4')], check=True)
            subprocess.run([ffmpeg, '-hide_banner', '-loglevel', 'error', '-i', str(clip), '-an', '-c:v', 'copy', str(work/'movies/Video Only.mp4')], check=True)
            subprocess.run([ffmpeg, '-hide_banner', '-loglevel', 'error', '-i', str(clip), '-itsoffset', '0.25', '-i', str(clip), '-map', '0:v', '-map', '1:a', '-c', 'copy', str(work/'movies/Delayed Audio.mp4')], check=True)
            subprocess.run([ffmpeg, '-hide_banner', '-loglevel', 'error', '-i', str(clip), '-filter_complex', '[0:a]atrim=duration=5[a]', '-map', '0:v', '-map', '[a]', '-c:v', 'copy', '-c:a', 'aac', str(work/'movies/Short Audio.mp4')], check=True)
        if args.long:
            subprocess.run([ffmpeg, '-hide_banner', '-loglevel', 'error', '-stream_loop', '-1', '-i', str(clip),
                            '-t', str(args.long_seconds), '-c:v', 'copy', '-af', 'aresample=async=1:first_pts=0', '-c:a', 'aac', '-ar', '48000', str(work/'movies/Long Stream.mp4')], check=True)
        with socket.socket() as sock:
            sock.bind(('127.0.0.1', 0))
            port = sock.getsockname()[1]
        base = f'http://127.0.0.1:{port}'
        (work/'config/network.xml').write_text(f'<NetworkConfiguration><InternalHttpPort>{port}</InternalHttpPort><PublicHttpPort>{port}</PublicHttpPort><EnableRemoteAccess>false</EnableRemoteAccess><EnableIPv6>false</EnableIPv6><LocalNetworkAddresses><string>127.0.0.1</string></LocalNetworkAddresses><EnableUPnP>false</EnableUPnP></NetworkConfiguration>')
        opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
        token = ''

        def api(path, body=None):
            headers = {'Content-Type': 'application/json', 'Authorization': 'MediaBrowser Client="XenonFinTests", Device="Host", DeviceId="native-video", Version="0.7"'+(', Token="'+token+'"' if token else '')}
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
                    if len(indexed) >= (3+int(args.long)+4*int(args.extended)):
                        break
                    time.sleep(.25)
                movie = next(i for i in indexed if i['Type'] == 'Movie' and i['Name'] == 'Native Stream')
                episode = next(i for i in indexed if i['Type'] == 'Episode')
                sync_movie = next(i for i in indexed if i['Name'] == 'Native Sync')
                cases = [(movie, '', 'movie/realtime/pause/resume/end'), (episode, '', 'episode/realtime'), (movie, '--stop', 'cancel/report/encoding cleanup'), (sync_movie, '--sync', 'known flash/beep A-V timing')]
                if args.extended:
                    for name, label in [('Video Only', 'silent source fallback'), ('Delayed Audio', 'positive audio PTS offset'), ('Short Audio', 'hardware-clocked silent video tail'), ('Gapped Audio', 'timestamp-gapped source PCM placement')]:
                        cases.append((next(i for i in indexed if i['Name'] == name), '--audio-gap' if name == 'Gapped Audio' else '--audio-tail' if name == 'Short Audio' else '', label))
                    cases.extend([(movie, '--mode-a', 'mode A same audio-bearing stream/video only'), (movie, '--mode-b', 'mode B same stream/audio only'), (movie, '--console-cost', 'console-cost cooperative decode and GPU'), (movie, '--short-stall', '60ms CPU stall covered by bounded DMA'), (movie, '--recovery', '250ms CPU stall and DMA recovery'), (movie, '--pause-long', '20-second pause/backpressure'), (movie, '--stall', '5-second network interruption/rebuffer'), (movie, '--slow-audio', '46 kHz simulated device vs 48 kHz media clock')])
                if args.long:
                    long_movie = next(i for i in indexed if i['Type'] == 'Movie' and i['Name'] == 'Long Stream')
                    cases.append((long_movie, '', f'long-duration {args.long_seconds}-second media/realtime HOST clock'))
                if args.case:
                    cases = [case for case in cases if args.case in case[2]]
                    if not cases:
                        raise ValueError('No matching test case')
                evidence = []
                for item, flag, label in cases:
                    command = [str(probe), base, item['Id']]+([flag] if flag else [])
                    result = subprocess.run(command, env={**env, 'JELLYFIN_ACCESS_TOKEN': token, 'JELLYFIN_USER_ID': user},
                                            capture_output=True, text=True, timeout=max(420,args.long_seconds+120))
                    if token in result.stdout+result.stderr or password in result.stdout+result.stderr:
                        raise RuntimeError('Secret appeared in probe output')
                    if result.returncode:
                        raise RuntimeError('Native video probe failed: '+result.stderr.strip())
                    metrics = json.loads(result.stdout)
                    if args.long and item.get('Name') == 'Long Stream':
                        assert metrics['decoded'] >= args.long_seconds*30-10 and metrics['received'] > 8*1024*1024, metrics
                    elif flag == '--audio-gap':
                        assert metrics['decoded'] >= 480 and metrics['gap_events'] > 0, metrics
                    elif not flag or flag == '--accelerated':
                        assert metrics['decoded'] == 240, metrics
                    assert metrics['reports_204'] and metrics['cleaned'], metrics
                    if flag != '--mode-b':
                        assert metrics['moving_rgb_frames'] > 20, metrics
                    if flag == '--sync':
                        assert metrics['sync_flashes'] == metrics['sync_beeps'] == 8, metrics
                    if metrics['audio_expected']:
                        assert metrics['audio_hz'] == 48000 and metrics['audio_frames'] > 0, metrics
                    if label == 'positive audio PTS offset':
                        assert metrics['audio_initial_offset_us'] > 100000, metrics
                    evidence.append({'case': label, **metrics})
                    if args.metrics:
                        args.metrics.write_text(json.dumps({'server': info['Version'], 'physical_xbox_tested': False, 'runs': evidence}, indent=2)+'\n')
                    print(label+': '+json.dumps(metrics), flush=True)
                print('Real Jellyfin '+info['Version']+' MPEG-1/MP3 PS, decoded PCM, consumption-clock video, flash/beep timing, pause/cancel/reports PASS; Xbox presentation and ten-minute console stability PENDING', flush=True)
            finally:
                process.terminate()
                try:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()


if __name__ == '__main__':
    main()
