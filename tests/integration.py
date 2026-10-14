#!/usr/bin/env python3
"""Protocol fixture + real codec smoke tests. This is not a real Jellyfin server.
Use tests/real_server.py for end-to-end checks against Jellyfin itself.
"""
import http.server
import json
import os
import pathlib
import socket
import ssl
import subprocess
import tempfile
import threading
import time
import urllib.parse

ROOT = pathlib.Path(__file__).resolve().parents[1]
BIN = ROOT / 'build/xenonfin'

def main():
    with tempfile.TemporaryDirectory(prefix='xenonfin-integration-') as directory:
        work = pathlib.Path(directory)
        sample = work / 'sample.mp4'
        subprocess.run(['ffmpeg', '-hide_banner', '-loglevel', 'error', '-f', 'lavfi', '-i', 'testsrc2=size=1280x720:rate=30',
                        '-f', 'lavfi', '-i', 'sine=frequency=440:sample_rate=48000', '-t', '3', '-c:v', 'libx264',
                        '-threads', '2', '-preset', 'ultrafast', '-profile:v', 'baseline', '-pix_fmt', 'yuv420p', '-b:v', '3M',
                        '-c:a', 'aac', '-ac', '2', '-movflags', '+faststart', str(sample)], check=True)
        for suffix, codec in [('mp3', 'libmp3lame'), ('m4a', 'aac'), ('flac', 'flac')]:
            subprocess.run(['ffmpeg', '-hide_banner', '-loglevel', 'error', '-f', 'lavfi', '-i', 'sine=frequency=660:sample_rate=48000',
                            '-t', '1', '-c:a', codec, '-ac', '2', str(work / ('audio.' + suffix))], check=True)
        subprocess.run(['ffmpeg','-hide_banner','-loglevel','error','-f','lavfi','-i','color=blue:size=600x900','-frames:v','1','-threads','1',str(work/'poster.png')],check=True)
        movie = {'Id': 'movie', 'Name': 'Test movie', 'Type': 'Movie', 'RunTimeTicks': 30000000, 'IsFolder': False}
        events = []
        leaked = []
        class Handler(http.server.BaseHTTPRequestHandler):
            reset_count = 0
            def log_message(self, *args):
                pass
            def reply(self, value, status=200, headers=None):
                data = value if isinstance(value, bytes) else json.dumps(value).encode()
                self.send_response(status)
                self.send_header('Content-Length', str(len(data)))
                for k, v in (headers or {}).items(): self.send_header(k, v)
                self.end_headers()
                if self.command != 'HEAD':
                    try: self.wfile.write(data)
                    except (BrokenPipeError, ConnectionResetError): pass
            def do_HEAD(self):
                self.do_GET()
            def do_GET(self):
                if 'do-not-forward-fixture-token' in self.headers.get('Authorization',''): leaked.append(True)
                path = urllib.parse.urlsplit(self.path).path
                if path == '/oversize': return self.reply(b'x' * 4096)
                if path == '/slow':
                    self.send_response(200); self.send_header('Content-Length', '10'); self.end_headers()
                    time.sleep(2)
                    try: self.wfile.write(b'0123456789')
                    except (BrokenPipeError, ConnectionResetError): pass
                    return
                if path == '/reset' and Handler.reset_count == 0:
                    Handler.reset_count += 1
                    self.connection.shutdown(socket.SHUT_RDWR); self.connection.close(); return
                if path in ('/reset', '/recovered'): return self.reply(b'recovered')
                if path in ('/redirect', '/foreign'):
                    return self.reply(b'', 302, {'Location': '/recovered' if path == '/redirect' else 'http://127.0.0.1:1/steal'})
                if path == '/System/Info/Public': return self.reply({'ServerName': 'Protocol fixture', 'Version': '10.10.7'})
                if path.startswith('/media/') or path == '/Videos/movie/stream':
                    if path == '/Videos/movie/stream' and 'fixture-token' not in self.headers.get('Authorization',''): return self.reply({},401)
                    file = sample if path == '/Videos/movie/stream' else work / pathlib.Path(path).name
                    if not file.is_file(): return self.reply({}, 404)
                    data = file.read_bytes()
                    start = 0; end = len(data)-1; status = 200; headers = {'Accept-Ranges': 'bytes'}
                    if self.headers.get('Range'):
                        span = self.headers['Range'].removeprefix('bytes=').split('-', 1)
                        start = int(span[0]); end = int(span[1]) if span[1] else end; status = 206
                        if start > end or start >= len(data): return self.reply({}, 416)
                        headers['Content-Range'] = f'bytes {start}-{end}/{len(data)}'
                    return self.reply(data[start:end+1], status, headers)
                if 'fixture-token' not in self.headers.get('Authorization', ''): return self.reply({}, 401)
                if path == '/Users/user': return self.reply({'Id': 'user', 'Name': 'Fixture user'})
                if path == '/Users/user/Views': return self.reply({'Items': [{'Id': 'library', 'Name': 'Movies', 'IsFolder': True}], 'TotalRecordCount': 1})
                if path == '/Users/user/Items': return self.reply({'Items': [movie], 'TotalRecordCount': 1})
                if path == '/Users/user/Items/movie': return self.reply(movie)
                return self.reply({}, 404)
            def do_POST(self):
                path = urllib.parse.urlsplit(self.path).path
                raw = self.rfile.read(int(self.headers.get('Content-Length', '0')))
                if path == '/redirect': return self.reply(b'', 302, {'Location': '/recovered'})
                data = json.loads(raw or b'null')
                if path == '/Users/AuthenticateByName':
                    if data.get('Username') == 'fixture' and data.get('Pw') == 'fixture-password':
                        return self.reply({'AccessToken': 'fixture-token', 'User': {'Id': 'user', 'Name': 'Fixture'}})
                    return self.reply({}, 401)
                if 'fixture-token' not in self.headers.get('Authorization', ''): return self.reply({}, 401)
                if path == '/Items/movie/PlaybackInfo':
                    assert data['DeviceProfile']['Name'].startswith('XenonFin desktop')
                    return self.reply({'PlaySessionId': 'session', 'MediaSources': [{'Id': 'source', 'Container': 'mp4', 'Bitrate': 3000000,
                        'SupportsDirectPlay': True, 'MediaStreams': [{'Type': 'Video', 'Codec': 'h264', 'Width': 1280, 'Height': 720},
                                                                   {'Type': 'Audio', 'Codec': 'aac', 'Channels': 2}]}]})
                if path.startswith('/Sessions/Playing'): events.append((path, data)); return self.reply(b'', 204)
                return self.reply({}, 404)
        server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        thread = threading.Thread(target=server.serve_forever, daemon=True); thread.start()
        base = f'http://127.0.0.1:{server.server_port}'
        env = {**os.environ, 'XENONFIN_SETTINGS': str(work / 'settings.json'), 'NO_PROXY': '127.0.0.1,localhost',
               'no_proxy': '127.0.0.1,localhost', 'JELLYFIN_USERNAME': 'fixture', 'JELLYFIN_PASSWORD': 'fixture-password',
               'JELLYFIN_ACCESS_TOKEN':'','JELLYFIN_USER_ID':''}
        def run(*args, expected=0):
            p = subprocess.run([str(BIN), *args], env=env, text=True, capture_output=True, timeout=30)
            if p.returncode != expected: raise RuntimeError(f'{args[0]} failed: {p.stderr.strip()} {p.stdout.strip()}')
            return json.loads(p.stdout) if p.stdout.strip() else None
        try:
            subprocess.run([str(ROOT / 'build/core-tests'), base], env=env, check=True, timeout=30)
            assert run('--probe', base)['connected']
            settings_path=work/'settings.json'
            settings_path.write_text(json.dumps({'server':'https://unrelated.example','token':'do-not-forward-fixture-token','user_id':'unrelated'}))
            settings_path.chmod(0o600)
            assert run('--probe',base)['connected']
            assert run('--integration', base)['playback_negotiated']
            assert not leaked, 'Saved credentials forwarded to a different server'
            settings_path.unlink()
            image=run('--artwork',base+'/media/poster.png','--headless')
            assert image['width']==240 and image['height']==360 and image['rgba_bytes']==240*360*4,image
            bad_env = {**env, 'JELLYFIN_PASSWORD': 'incorrect'}
            bad = subprocess.run([str(BIN), '--integration', base], env=bad_env, capture_output=True, text=True, timeout=20)
            assert bad.returncode == 1 and 'Authentication' in bad.stderr
            assert 'fixture-token' not in bad.stdout + bad.stderr and 'incorrect' not in bad.stdout + bad.stderr
            for source in (str(sample), base + '/media/sample.mp4'):
                result = run('--play', source, '--headless', '--seconds', '10')
                assert result['decoded_video_frames'] == 90, result
                assert result['displayed_video_frames'] >= 80, result
                assert result['decoded_audio_samples'] >= 140000, result
                assert result['video_codec']=='h264' and result['audio_codec']=='aac' and result['width']==1280 and result['height']==720,result
                print('720p desktop playback:', json.dumps(result))
            result = run('--play', base + '/media/sample.mp4', '--headless', '--seconds', '10', '--exercise')
            assert result['seek_exercised'] and result['pause_exercised'] and result['final_position_seconds'] >= 2.5, result
            print('Seek/pause desktop playback:', json.dumps(result))
            for suffix in ('mp3', 'm4a', 'flac'):
                result = run('--play', base + '/media/audio.' + suffix, '--headless', '--seconds', '5')
                assert result['decoded_audio_samples'] >= 45000, result
                print('Desktop audio', suffix, ':', result['decoded_audio_samples'], 'samples')
            run('--headless', '--seconds', '0.5')
            subprocess.run([str(ROOT/'build/ui-tests'),base],env=env,check=True,timeout=25)
            cert = work / 'cert.pem'; key = work / 'key.pem'
            subprocess.run(['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-keyout', str(key), '-out', str(cert),
                            '-days', '1', '-subj', '/CN=localhost'], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            tls = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
            ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER); ctx.load_cert_chain(cert, key)
            tls.socket = ctx.wrap_socket(tls.socket, server_side=True)
            threading.Thread(target=tls.serve_forever, daemon=True).start()
            p = subprocess.run([str(BIN), '--probe', f'https://localhost:{tls.server_port}'], env=env, capture_output=True, text=True, timeout=15)
            assert p.returncode == 1 and 'certificate' in p.stderr.lower(), p.stderr
            tls.shutdown(); tls.server_close()
            print('Protocol fixture, cancellation, recovery, TLS rejection, codecs, seeking, and headless UI: PASS')
        finally:
            server.shutdown(); server.server_close(); thread.join()

if __name__ == '__main__': main()
