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
                (320, 240, '30000/1001', 2, 2, 60)]):
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
            assert mean < 1.0 and max(differences) < 32, (mean, max(differences))
            frame_size = w * h * 3 // 2
            assert hashlib.sha256(a[:frame_size]).digest() != hashlib.sha256(a[-frame_size:]).digest()
            metadata['mean_abs_pixel_error'] = round(mean, 6)
            metadata['max_abs_pixel_error'] = max(differences)
            evidence.append(metadata)
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
    print('Native scalar MPEG-1 I/P/B frames, 24/29.97/30 FPS, padded planes, timestamps and format errors PASS')


if __name__ == '__main__':
    main()
