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
import sys
import tempfile
import time
import urllib.error
import urllib.request
import urllib.parse
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[1]

def main():
    music = '--music' in sys.argv[1:]
    browsing = '--browse' in sys.argv[1:] or music
    binary = pathlib.Path(os.environ.get('JELLYFIN_TEST_SERVER', '/workspace/setup/jellyfin-server/jellyfin'))
    probe = pathlib.Path(os.environ.get('JELLYFIN_NATIVE_PROBE', str(ROOT / 'build/native-tests/probe')))
    if not binary.is_file() or not probe.is_file():
        raise RuntimeError('Build make native-test and install the existing test server first')
    with tempfile.TemporaryDirectory(prefix='xenonfin-native-http-') as temporary:
        work = pathlib.Path(temporary)
        for name in ('data', 'config', 'cache', 'logs'):
            (work/name).mkdir()
        if browsing:
            for name in ('movies', 'tv', 'music', 'empty', 'other'):
                (work/name).mkdir()
            # Metadata-only movie/TV entries; no video generation or playback.
            for index in range(12):
                folder = work/'movies'/f'Native Movie {index:02d} (2024)'
                folder.mkdir()
                (folder/'movie.strm').write_text('http://127.0.0.1:9/never-played.mp4\n')
                (folder/'movie.nfo').write_text(f'<movie><title>Native Movie {index:02d}</title><year>2024</year><plot>Native movie synopsis {index}</plot><genre>Drama</genre></movie>')
            series = work/'tv/Native Series'
            season = series/'Season 01'
            season.mkdir(parents=True)
            (series/'tvshow.nfo').write_text('<tvshow><title>Native Series</title><plot>Native series synopsis</plot></tvshow>')
            (season/'Native Series S01E01.strm').write_text('http://127.0.0.1:9/never-played.mp4\n')
            (season/'Native Series S01E01.nfo').write_text('<episodedetails><title>Native Episode One</title><season>1</season><episode>1</episode><plot>Native episode synopsis</plot></episodedetails>')
            with zipfile.ZipFile(work/'other/Native Book.epub', 'w') as book:
                book.writestr('mimetype', 'application/epub+zip')
                book.writestr('META-INF/container.xml', '<?xml version="1.0"?><container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="content.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
                book.writestr('content.opf', '<?xml version="1.0"?><package xmlns="http://www.idpf.org/2007/opf" version="3.0" unique-identifier="id"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:identifier id="id">native-test-book</dc:identifier><dc:title>Native Book</dc:title><dc:language>en</dc:language></metadata><manifest><item id="text" href="text.xhtml" media-type="application/xhtml+xml"/></manifest><spine><itemref idref="text"/></spine></package>')
                book.writestr('text.xhtml', '<html xmlns="http://www.w3.org/1999/xhtml"><head><title>Native Book</title></head><body><p>Metadata browsing fixture.</p></body></html>')
            album = work/'music/Native Artist/Native Album'
            album.mkdir(parents=True)
            for index in (1, 2):
                signal = ('sine=frequency=440:sample_rate=44100' if index == 1 else 'aevalsrc=0.2*sin(2*PI*440*t)|0.2*sin(2*PI*997*t):s=48000') if music else 'anullsrc=r=44100:cl=stereo'
                subprocess.run(['ffmpeg', '-hide_banner', '-loglevel', 'error', '-f', 'lavfi', '-i', signal,
                    '-t', '12' if music else '1', '-ac', '1' if music and index == 1 else '2', '-c:a', 'libmp3lame', '-b:a', '192k', '-metadata', 'artist=Native Artist', '-metadata', 'album_artist=Native Artist',
                    '-metadata', 'album=Native Album', '-metadata', f'title=Native Track {index}', '-metadata', f'track={index}',
                    str(album/f'{index:02d} Native Track.mp3')], check=True)
        with socket.socket() as s:
            s.bind(('127.0.0.1', 0))
            port = s.getsockname()[1]
        base = f'http://127.0.0.1:{port}'
        (work/'config/network.xml').write_text(f'''<?xml version="1.0" encoding="utf-8"?>
<NetworkConfiguration><InternalHttpPort>{port}</InternalHttpPort><PublicHttpPort>{port}</PublicHttpPort><EnableRemoteAccess>false</EnableRemoteAccess><EnableIPv6>false</EnableIPv6><LocalNetworkAddresses><string>127.0.0.1</string></LocalNetworkAddresses><EnableUPnP>false</EnableUPnP></NetworkConfiguration>''')
        opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
        token = ''
        def api(path, body=None, method=None):
            data = None if body is None else json.dumps(body).encode()
            headers = {'Content-Type': 'application/json', 'Authorization': 'MediaBrowser Client="XenonFinTests", Device="Host", DeviceId="native-integration", Version="0.3.0"'+(', Token="'+token+'"' if token else '')}
            request = urllib.request.Request(base+path, data=data, headers=headers, method=method)
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
                if browsing:
                    auth = api('/Users/AuthenticateByName', {'Username': username, 'Pw': password})
                    token = auth['AccessToken']
                    user_id = auth['User']['Id']
                    for name, kind, folder in (('Native Movies', 'movies', 'movies'), ('Native TV', 'tvshows', 'tv'),
                                               ('Native Music', 'music', 'music'), ('Native Empty', 'movies', 'empty'),
                                               ('Native Other', 'books', 'other')):
                        api('/Library/VirtualFolders?name='+urllib.parse.quote(name)+'&collectionType='+kind+'&refreshLibrary=true',
                            {'LibraryOptions': {'PathInfos': [{'Path': str(work/folder)}], 'EnableRealtimeMonitor': False,
                            'TypeOptions': [{'Type': t, 'MetadataFetchers': [], 'ImageFetchers': []} for t in ('Movie', 'Series', 'Season', 'Episode', 'MusicArtist', 'MusicAlbum', 'Audio', 'Video')]}})
                    for _ in range(240):
                        indexed = api('/Users/'+user_id+'/Items?Recursive=true&IncludeItemTypes=Movie,Episode,Audio,Book&EnableImages=false')['Items']
                        if len(indexed) >= 16:
                            break
                        time.sleep(.25)
                    if len(indexed) < 16:
                        counts = {kind: sum(i['Type'] == kind for i in indexed) for kind in ('Movie', 'Episode', 'Audio', 'Book')}
                        raise RuntimeError('Real Jellyfin did not index the metadata/audio fixtures: '+str(counts))
                    result = subprocess.run([str(probe), base, '--browse'], env={**env, 'JELLYFIN_USERNAME': username, 'JELLYFIN_PASSWORD': password},
                        capture_output=True, text=True, timeout=120)
                    if result.returncode:
                        raise RuntimeError('Real native browsing failed: '+result.stderr.strip())
                    if token in result.stdout+result.stderr or password in result.stdout+result.stderr:
                        raise RuntimeError('Secret appeared in native output')
                    print(result.stdout.strip(), flush=True)
                    # Verify /Views obeys the logged-in user's folder permissions.
                    restricted_password = secrets.token_urlsafe(24)
                    restricted = api('/Users/New', {'Name': 'native-restricted'})
                    api('/Users/'+restricted['Id']+'/Password', {'CurrentPw': '', 'NewPw': restricted_password})
                    views = api('/Users/'+user_id+'/Views')['Items']
                    movie_id = next(i['Id'] for i in views if i['Name'] == 'Native Movies')
                    policy = restricted['Policy']
                    policy.update({'IsAdministrator': False, 'EnableAllFolders': False, 'EnabledFolders': [movie_id]})
                    api('/Users/'+restricted['Id']+'/Policy', policy)
                    result = subprocess.run([str(probe), base, '--restricted'], env={**env, 'JELLYFIN_USERNAME': 'native-restricted', 'JELLYFIN_PASSWORD': restricted_password},
                        capture_output=True, text=True, timeout=120)
                    if result.returncode:
                        raise RuntimeError('Restricted native browsing failed: '+result.stderr.strip())
                    print(result.stdout.strip(), flush=True)
                    if music:
                        album = work/'music/Native Artist/Native Album'
                        subprocess.run(['ffmpeg', '-hide_banner', '-loglevel', 'error', '-f', 'lavfi', '-i', 'sine=frequency=660:sample_rate=44100',
                            '-t', '2', '-c:a', 'flac', '-metadata', 'artist=Native Artist', '-metadata', 'album_artist=Native Artist',
                            '-metadata', 'album=Native Album', '-metadata', 'title=Native Track 3', '-metadata', 'track=3',
                            str(album/'03 Native Track.flac')], check=True)
                        api('/Library/Refresh', {}, 'POST')
                        for _ in range(160):
                            tracks = api('/Users/'+user_id+'/Items?Recursive=true&IncludeItemTypes=Audio&EnableImages=false')['Items']
                            if len(tracks) == 3:
                                break
                            time.sleep(.25)
                        if len(tracks) != 3:
                            raise RuntimeError('Real server did not index the additional audio transcoding fixture')
                        music_probe = pathlib.Path(os.environ.get('JELLYFIN_MUSIC_PROBE', str(ROOT/'build/native-tests/music-probe')))
                        result = subprocess.run([str(music_probe), base], env={**env, 'JELLYFIN_ACCESS_TOKEN': token, 'JELLYFIN_USER_ID': user_id},
                            capture_output=True, text=True, timeout=90)
                        if result.returncode or result.stderr.strip():
                            raise RuntimeError('Real native music playback failed: '+result.stderr.strip())
                        if token in result.stdout+result.stderr or password in result.stdout+result.stderr:
                            raise RuntimeError('Secret appeared in native playback output')
                        print(result.stdout.strip(), flush=True)
                        for track in tracks:
                            detail = api('/Users/'+user_id+'/Items/'+track['Id'])
                            if detail.get('UserData', {}).get('PlayCount', 0) < 1:
                                raise RuntimeError('Real Jellyfin did not record native music playback start: '+track['Type'])
                        sessions = api('/Sessions')
                        if any(session.get('NowPlayingItem') for session in sessions):
                            raise RuntimeError('Real Jellyfin playback stop report did not clear the playing item')
                        print('Real Jellyfin native music start/progress/stop, MP3 direct streams, FLAC→MP3 and transcoder cleanup PASS', flush=True)
            finally:
                process.terminate()
                try:
                    process.wait(timeout=15)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()

if __name__ == '__main__':
    main()
