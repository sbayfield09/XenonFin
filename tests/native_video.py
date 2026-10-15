#!/usr/bin/env python3
"""Real MPEG-1 decoding through the same native C codec, compared to FFmpeg.
No GPU/console claim: the host probe writes YUV and monotonic source timestamps.
"""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
PROBE = Path(os.environ.get('JELLYFIN_VIDEO_PROBE', ROOT / 'build/native-tests/video-probe'))
STREAM_PROBE = Path(os.environ.get('JELLYFIN_VIDEO_STREAM_DECODE_PROBE', ROOT / 'build/native-tests/stream-decode-probe'))


def run(args, **kwargs):
    return subprocess.run(args, check=True, capture_output=True, timeout=60, **kwargs)


def encode(path, width, height, rate, seconds, bframes=0):
    run(['ffmpeg', '-hide_banner', '-loglevel', 'error', '-f', 'lavfi', '-i',
         f'testsrc2=size={width}x{height}:rate={rate}', '-t', str(seconds), '-an',
         '-c:v', 'mpeg1video', '-bf', str(bframes), '-g', '12', '-q:v', '5',
         '-pix_fmt', 'yuv420p', '-aspect', f'{width}:{height}', '-f', 'mpeg1video', '-y', str(path)])


def main():
    evidence = []
    with tempfile.TemporaryDirectory(prefix='xenonfin-native-video-') as directory:
        folder = Path(directory)
        for index, (w, h, rate, seconds, bframes, count) in enumerate([
                (320, 240, '30', 8, 0, 240), (640, 360, '24', 2, 2, 48),
                (320, 240, '30000/1001', 2, 2, 60),
                (640, 360, '30', 2, 0, 60), (720, 480, '30', 2, 0, 60)]):
            clip = folder / f'{index}.m1v'
            actual = folder / 'actual.yuv'
            reference = folder / 'reference.yuv'
            encode(clip, w, h, rate, seconds, bframes)
            metadata = json.loads(run([str(PROBE), str(clip), str(actual)]).stdout)
            run(['ffmpeg', '-hide_banner', '-loglevel', 'error', '-i', str(clip),
                 '-pix_fmt', 'yuv420p', '-f', 'rawvideo', '-y', str(reference)])
            a, b = actual.read_bytes(), reference.read_bytes()
            assert len(a) == len(b) == w * h * 3 // 2 * count, metadata
            assert metadata['width'] == w and metadata['height'] == h and metadata['frames'] == count
            assert abs(metadata['last_pts_us'] - round((count - 1) / metadata['fps'] * 1e6)) <= 1
            differences = bytearray(abs(x - y) for x, y in zip(a, b))
            mean = sum(differences) / len(differences)
            assert mean < 1.0 and max(differences) < 32, (metadata, mean, max(differences))
            frame_size = w * h * 3 // 2
            assert hashlib.sha256(a[:frame_size]).digest() != hashlib.sha256(a[-frame_size:]).digest()
            metadata['mean_abs_pixel_error'] = round(mean, 6)
            metadata['max_abs_pixel_error'] = max(differences)
            # Same codec through incremental PS framing, arbitrary byte splits,
            # bounded ES ring and EOF drain: all decoded pixels must match.
            ps, incremental = folder / 'wrapped.mpg', folder / 'incremental.yuv'
            run(['ffmpeg', '-hide_banner', '-loglevel', 'error', '-i', str(clip),
                 '-c:v', 'copy', '-f', 'mpeg', '-y', str(ps)])
            streamed = json.loads(run([str(STREAM_PROBE), str(ps), str(incremental)]).stdout)
            assert streamed['frames'] == count and incremental.read_bytes() == a, streamed
            metadata['incremental_ps_frames'] = streamed['frames']
            evidence.append(metadata)
        # Actual compressed RGB patches -> native decoder -> actual CPU tiled
        # converter -> emulated PPC BGRA bytes. Compare to independent FFmpeg.
        clip = folder / 'colours.m1v'
        patches = '|'.join(['black', 'white', 'red', 'lime', 'blue', 'cyan', 'magenta', 'yellow'])
        source = 'color=c=black:s=320x240:r=30'
        for x, colour in enumerate(patches.split('|')):
            source += f',drawbox=x={x*40}:y=0:w=40:h=240:color={colour}:t=fill'
        run(['ffmpeg', '-hide_banner', '-loglevel', 'error', '-f', 'lavfi', '-i', source,
             '-frames:v', '1', '-c:v', 'mpeg1video', '-bf', '0', '-q:v', '2',
             '-pix_fmt', 'yuv420p', '-f', 'mpeg1video', '-y', str(clip)])
        actual_rgb, expected_rgb = folder / 'actual.rgb', folder / 'expected.rgb'
        meta = json.loads(run([str(PROBE), str(clip), str(actual), str(actual_rgb)]).stdout)
        assert meta['frames'] == 1, meta
        run(['ffmpeg', '-hide_banner', '-loglevel', 'error', '-i', str(clip),
             '-pix_fmt', 'rgb24', '-f', 'rawvideo', '-y', str(expected_rgb)])
        a, b = actual_rgb.read_bytes(), expected_rgb.read_bytes()
        assert len(a) == len(b) == 320 * 240 * 3
        errors = [abs(x-y) for x, y in zip(a, b)]
        assert max(errors) <= 5 and sum(errors)/len(errors) < 2.0
        print(f'Encoded RGB patches/native tiled PPC packing vs FFmpeg: MAE={sum(errors)/len(errors):.3f}, max={max(errors)}')
        # Validate native wrapper rejection before decoder allocation.
        baseline = (folder / '0.m1v').read_bytes()
        bad = folder / 'bad.m1v'
        invalid_picture = bytearray(baseline)
        picture = baseline.index(b'\x00\x00\x01\x00')
        invalid_picture[picture + 5] &= 0xc7
        for content in [b'x' * 256, baseline[:80], invalid_picture,
                        baseline[:4] + b'\xff\xff\xff' + baseline[7:],
                        baseline + b'\x00\x00\x01\xb5\x00\x00\x00\x00',
                        baseline + b'\x00\x00\x01\xb3\x28\x01\x68\x15']:
            bad.write_bytes(content)
            result = subprocess.run([str(PROBE), str(bad), str(actual)], capture_output=True, timeout=10)
            assert result.returncode == 1 and result.stderr, result
    print(json.dumps(evidence, indent=2))
    print('Native MPEG-1 through 720x480/30, I/P/B, BE colours, padded planes, timestamps and format errors PASS')


if __name__ == '__main__':
    main()
