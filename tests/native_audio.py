#!/usr/bin/env python3
"""Actual native MP3 decoder/resampler tests, without a sound device.
Synthetic tones are encoded as MP3 first. This does not establish console sound.
"""
import array
import json
import math
import os
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]

def main():
    probe = pathlib.Path(os.environ.get('JELLYFIN_DECODE_PROBE', str(ROOT/'build/native-tests/decode-probe')))
    with tempfile.TemporaryDirectory(prefix='xenonfin-native-decode-') as temporary:
        work = pathlib.Path(temporary)
        for rate, channels, seconds in ((8000, 1, 2), (16000, 1, 2), (22050, 2, 2), (32000, 2, 2), (44100, 1, 2), (44100, 2, 60), (48000, 2, 2)):
            source = work/f'{rate}-{channels}.mp3'
            pcm = work/'decoded.pcm'
            signal = 'sin(2*PI*440*t)' if channels == 1 else 'sin(2*PI*440*t)|sin(2*PI*997*t)'
            subprocess.run(['ffmpeg', '-hide_banner', '-loglevel', 'error', '-f', 'lavfi', '-i',
                f'aevalsrc=0.2*{signal}:s={rate}:d={seconds}', '-ac', str(channels), '-c:a', 'libmp3lame',
                '-b:a', '64k' if rate < 32000 else '192k', str(source)], check=True)
            run = subprocess.run([str(probe), str(source), str(pcm)], capture_output=True, text=True, timeout=45)
            if run.returncode:
                raise RuntimeError(run.stderr)
            if run.stderr.strip():
                raise RuntimeError('Decoder sanitizer/warning output: '+run.stderr.strip())
            metrics = json.loads(run.stdout)
            assert metrics['input_rate'] == rate and metrics['channels'] == channels, metrics
            assert metrics['output_frames'] == (metrics['source_frames']*48000+rate-1)//rate, metrics
            raw = pcm.read_bytes()
            assert len(raw) == metrics['output_frames']*4
            assert seconds <= metrics['output_frames']/48000 < seconds+.3, metrics
            samples = array.array('h', raw)
            if sys.byteorder != 'little':
                samples.byteswap()
            left = samples[48000:96000:2]
            right = samples[48001:96000:2]
            assert left and max(abs(s) for s in left) > 2000, 'Decoded PCM is silent'
            if channels == 1:
                assert left == right, 'Mono expansion is not identical in both channels'
            for channel, frequency in ((left, 440), (right, 440 if channels == 1 else 997)):
                # Frequency-domain evidence that real MP3 audio was decoded and
                # resampled at the intended rate, with correct LE sample packing.
                energy = sum(v*v for v in channel)/len(channel)
                real = sum(v*math.cos(2*math.pi*frequency*i/48000) for i, v in enumerate(channel))/len(channel)
                imag = sum(v*math.sin(2*math.pi*frequency*i/48000) for i, v in enumerate(channel))/len(channel)
                assert 2*(real*real+imag*imag)/energy > .95, (rate, channels, frequency)
            print('Native MP3 decode → 48 kHz stereo S16LE:', json.dumps(metrics), flush=True)
        # A large ID3 tag spans multiple compressed ring capacities. It must be
        # skipped incrementally, without allocating or retaining the full tag.
        original = (work/'48000-2.mp3').read_bytes()
        if original[:3] == b'ID3':
            size = 0
            for value in original[6:10]:
                size = (size << 7) | value
            original = original[10+size:]
        size = 300000
        header = b'ID3\x03\x00\x00'+bytes((size >> shift) & 127 for shift in (21, 14, 7, 0))
        tagged = work/'large-tag.mp3'
        tagged.write_bytes(header+b'\x00'*size+original)
        run = subprocess.run([str(probe), str(tagged), str(work/'tagged.pcm')], capture_output=True, text=True, timeout=15)
        assert run.returncode == 0 and not run.stderr, run.stderr
        print('Native large-ID3 streaming across ring boundaries PASS', flush=True)

if __name__ == '__main__':
    main()
