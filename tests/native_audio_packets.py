#!/usr/bin/env python3
"""Actual MP3 in fragmented PS -> native decoder; compare complete ES and PCM."""
import os
from pathlib import Path
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[1]
probe=Path(os.environ.get('XENONFIN_AUDIO_PS_PROBE',ROOT/'build/native-tests/audio-ps-probe'))
decode=Path(os.environ.get('XENONFIN_DECODE_PROBE',ROOT/'build/native-tests/decode-probe'))
with tempfile.TemporaryDirectory(prefix='xenonfin-audio-packets-') as d:
    p=Path(d)
    subprocess.run(['ffmpeg','-hide_banner','-loglevel','error','-f','lavfi','-i','testsrc2=s=320x240:r=30',
                    '-f','lavfi','-i','aevalsrc=0.2*sin(2*PI*440*t)|0.2*sin(2*PI*997*t):s=48000',
                    '-t','20','-c:v','mpeg1video','-b:v','800k','-c:a','libmp3lame','-b:a','128k','-f','mpeg',str(p/'clip.mpeg')],check=True)
    subprocess.run(['ffmpeg','-hide_banner','-loglevel','error','-i',str(p/'clip.mpeg'),'-map','0:a:0','-c:a','copy',
                    '-id3v2_version','0','-write_id3v1','0','-write_xing','0',str(p/'reference.mp3')],check=True)
    subprocess.run([str(probe),str(p/'clip.mpeg'),str(p/'demux.mp3'),str(p/'ps.pcm')],check=True)
    assert (p/'reference.mp3').read_bytes()==(p/'demux.mp3').read_bytes(),'MP3 PES byte sequence differs'
    assert (p/'demux.mp3').stat().st_size>128*1024,'Fixture must wrap the compressed ring'
    subprocess.run([str(decode),str(p/'reference.mp3'),str(p/'reference.pcm')],check=True)
    assert (p/'reference.pcm').read_bytes()==(p/'ps.pcm').read_bytes(),'Fragmentation changed decoded samples'
    assert (p/'ps.pcm').stat().st_size>20*48000*4,'Fixture must wrap the PCM ring repeatedly'
print('Actual PS MP3 reconstruction and every decoded stereo PCM byte match standalone MP3 decode PASS (host only)')
