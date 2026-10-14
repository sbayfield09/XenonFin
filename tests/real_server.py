#!/usr/bin/env python3
"""End-to-end tests against a disposable, real Jellyfin 10.10.7 server.

Creates only synthetic media and a temporary test account. Never connects to
the user's server, modifies their library, or requires their credentials.
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
import urllib.parse
import urllib.request

ROOT=pathlib.Path(__file__).resolve().parents[1]
BIN=ROOT/'build/xenonfin'

def main():
    server_binary=pathlib.Path(os.environ.get('JELLYFIN_TEST_SERVER','/workspace/setup/jellyfin-server/jellyfin'))
    if not server_binary.is_file(): raise RuntimeError('Run scripts/fetch_test_server.py first')
    with tempfile.TemporaryDirectory(prefix='xenonfin-real-jellyfin-') as temporary:
        work=pathlib.Path(temporary)
        for p in ('data','config','cache','logs','movies','music'): (work/p).mkdir()
        with socket.socket() as s: s.bind(('127.0.0.1',0));port=s.getsockname()[1]
        base=f'http://127.0.0.1:{port}'
        (work/'config/network.xml').write_text(f'''<?xml version="1.0" encoding="utf-8"?>
<NetworkConfiguration><InternalHttpPort>{port}</InternalHttpPort><PublicHttpPort>{port}</PublicHttpPort><EnableRemoteAccess>false</EnableRemoteAccess><EnableIPv6>false</EnableIPv6><LocalNetworkAddresses><string>127.0.0.1</string></LocalNetworkAddresses><EnableUPnP>false</EnableUPnP></NetworkConfiguration>''')
        h264=work/'movies/Native H264 Test (2024)';h264.mkdir()
        hevc=work/'movies/Native HEVC Test (2024)';hevc.mkdir()
        subprocess.run(['ffmpeg','-hide_banner','-loglevel','error','-f','lavfi','-i','testsrc2=size=1280x720:rate=30',
                        '-f','lavfi','-i','sine=frequency=440:sample_rate=48000','-t','3','-c:v','libx264','-threads','2','-preset','ultrafast',
                        '-profile:v','baseline','-b:v','3M','-c:a','aac','-ac','2','-movflags','+faststart',str(h264/'Native H264 Test (2024).mp4')],check=True)
        subprocess.run(['ffmpeg','-hide_banner','-loglevel','error','-f','lavfi','-i','testsrc2=size=640x360:rate=30',
                        '-f','lavfi','-i','sine=frequency=440:sample_rate=48000','-t','3','-c:v','libx265','-threads','1','-preset','ultrafast',
                        '-x265-params','pools=none:frame-threads=1:log-level=error','-c:a','aac','-ac','2',str(hevc/'Native HEVC Test (2024).mp4')],check=True,stderr=subprocess.DEVNULL)
        album=work/'music/Test Artist/Test Album';album.mkdir(parents=True)
        for n in (1,2):
            subprocess.run(['ffmpeg','-hide_banner','-loglevel','error','-f','lavfi','-i','sine=frequency=440:sample_rate=48000','-t','2','-c:a','libmp3lame',
                            '-metadata','artist=Test Artist','-metadata','album=Test Album','-metadata',f'title=Test Track {n}','-metadata',f'track={n}',str(album/f'{n:02d} Test Track.mp3')],check=True)
        username='xenonfin-test';password=secrets.token_urlsafe(24)
        # Disable inherited HTTP proxies for this test's loopback traffic.
        opener=urllib.request.build_opener(urllib.request.ProxyHandler({}))
        token=''
        def api(path,method='GET',body=None,auth=True):
            headers={'Content-Type':'application/json','Authorization':'MediaBrowser Client="XenonFinTests", Device="Desktop", DeviceId="integration-test", Version="0.1.0"'+(', Token="'+token+'"' if token and auth else '')}
            data=None if body is None else json.dumps(body).encode()
            with opener.open(urllib.request.Request(base+path,data=data,headers=headers,method=method),timeout=15) as response:
                data=response.read()
                return json.loads(data) if data else None
        env={**os.environ,'NO_PROXY':'127.0.0.1,localhost','no_proxy':'127.0.0.1,localhost','DOTNET_CLI_TELEMETRY_OPTOUT':'1'}
        with (work/'server-console.log').open('wb') as logfile:
            process=subprocess.Popen([str(server_binary),'--datadir',str(work/'data'),'--configdir',str(work/'config'),'--cachedir',str(work/'cache'),
                '--logdir',str(work/'logs'),'--nowebclient','--ffmpeg','/usr/bin/ffmpeg','--nonetchange'],env=env,stdout=logfile,stderr=subprocess.STDOUT)
            try:
                ready=False
                for _ in range(120):
                    if process.poll() is not None: raise RuntimeError('Jellyfin test server exited during startup')
                    try:
                        info=api('/System/Info/Public',auth=False);ready=True;break
                    except (urllib.error.URLError,TimeoutError): time.sleep(.25)
                if not ready: raise RuntimeError('Jellyfin test server did not start')
                api('/Startup/Configuration','POST',{'UICulture':'en-US','MetadataCountryCode':'GB','PreferredMetadataLanguage':'en'},False)
                api('/Startup/User',auth=False)
                api('/Startup/User','POST',{'Name':username,'Password':password},False)
                api('/Startup/RemoteAccess','POST',{'EnableRemoteAccess':False,'EnableAutomaticPortMapping':False},False)
                api('/Startup/Complete','POST',{},False)
                auth=api('/Users/AuthenticateByName','POST',{'Username':username,'Pw':password},False)
                token=auth['AccessToken'];user_id=auth['User']['Id']
                for name,kind,folder in (('Test Movies','movies','movies'),('Test Music','music','music')):
                    api('/Library/VirtualFolders?name='+urllib.parse.quote(name)+'&collectionType='+kind+'&refreshLibrary=true','POST',
                        {'LibraryOptions':{'PathInfos':[{'Path':str(work/folder)}],'EnableRealtimeMonitor':False,
                         'TypeOptions':[{'Type':t,'MetadataFetchers':[],'ImageFetchers':[]} for t in ('Movie','MusicArtist','MusicAlbum','Audio')]}})
                items=[]
                for _ in range(160):
                    items=api('/Users/'+user_id+'/Items?Recursive=true&IncludeItemTypes=Movie,Audio&Fields=MediaSources')['Items']
                    if len(items)>=4: break
                    time.sleep(.25)
                if len(items)<4: raise RuntimeError('Real server did not index synthetic movie/music libraries')
                test_env={**env,'XENONFIN_SETTINGS':str(work/'client-settings.json'),'JELLYFIN_USERNAME':username,'JELLYFIN_PASSWORD':password,'JELLYFIN_ACCESS_TOKEN':'','JELLYFIN_USER_ID':''}
                def client(*args,extra=None):
                    p=subprocess.run([str(BIN),*args],env={**test_env,**(extra or {})},capture_output=True,text=True,timeout=40)
                    if p.returncode: raise RuntimeError('Client failed against real Jellyfin: '+p.stderr.strip())
                    return json.loads(p.stdout) if p.stdout.strip() else None
                summary=client('--integration',base)
                assert summary['authenticated'] and summary['libraries']==2 and summary['media_items']>=4 and summary['search_matches']>=1
                print('Real Jellyfin',info['Version'],'API:',json.dumps(summary),flush=True)
                for name in ('Native H264 Test','Native HEVC Test','Music track'):
                    item=next(i for i in items if (i['Type']=='Audio' if name=='Music track' else name in i['Name']))
                    result=client('--integration-play',base,'--headless','--seconds','12',extra={'JELLYFIN_TEST_ITEM_ID':item['Id']})
                    assert result['decoded_audio_samples']>40000,result
                    if item['Type']=='Movie': assert result['decoded_video_frames']>=60 and result['displayed_video_frames']>=55,result
                    if name=='Native HEVC Test': assert result['play_method']=='Transcode' and result['video_codec']=='h264' and result['audio_codec']=='aac' and result['container']=='mpegts',result
                    elif name=='Native H264 Test': assert result['play_method']=='DirectPlay' and result['width']==1280 and result['height']==720,result
                    print('Real server playback',name,':',json.dumps(result),flush=True)
                    user_item=api('/Users/'+user_id+'/Items/'+item['Id'])
                    assert user_item['UserData']['PlayCount']>=1, user_item['UserData']
                settings={'server':base,'token':token,'user_id':user_id,'user_name':username,'device_id':'real-server-ui-test','video_bitrate':4000000,'remember_login':True}
                path=work/'client-settings.json';path.write_text(json.dumps(settings));path.chmod(0o600)
                client('--headless','--seconds','1')
                print('Real server authentication, views, search, direct video, HEVC transcoding, music, reporting, saved token, and UI startup: PASS',flush=True)
            finally:
                process.terminate()
                try:process.wait(timeout=15)
                except subprocess.TimeoutExpired:process.kill();process.wait()

if __name__=='__main__': main()
