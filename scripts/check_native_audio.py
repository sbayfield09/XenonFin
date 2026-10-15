#!/usr/bin/env python3
"""Inspect linked SDK provenance/isolation; never claims physical audio works."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
ROOT = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--output', type=Path)
a = p.parse_args()
devkit = Path(os.environ.get('DEVKITXENON', '/workspace/setup/devkit'))
def tool(name, *args):
    return subprocess.check_output([str(devkit/'bin'/('xenon-'+name)), *map(str,args)])
archive = devkit/'usr/lib/libxenon.a'
sound_hash = hashlib.sha256(tool('ar','p',archive,'sound.o')).hexdigest()
assert sound_hash == '9a569f3a768d93da6a5ea9e4d3091a12b62b9ea1a9f41d0d5fc2d16b713d9692', 'Use the pinned SDK or re-audit the changed sound object'
report = {'physical_xbox_tested': False, 'driver': 'unmodified pinned libxenon.a(sound.o)',
          'sound_object_sha256': sound_hash, 'sdk_format': '48000 Hz signed 16-bit little-endian interleaved stereo; 4 bytes/frame', 'builds': []}
for name, isolated in [('xenonfin-boot',False), ('xenonfin-audio-test',True)]:
    elf = ROOT/'build/xenon'/f'{name}.elf'
    mapping = elf.with_suffix('.elf.map').read_text()
    symbols = tool('nm','-n',elf).decode()
    for fn in ('xenon_sound_init','xenon_sound_submit','xenon_sound_get_free','xenon_sound_get_unplayed'):
        assert re.search(r'\.text\.'+fn+r'\s+0x[0-9a-f]+\s+0x[0-9a-f]+\s+[^\n]*libxenon\.a\(sound\.o\)',mapping), fn
        assert len(re.findall(r' T '+fn+r'$',symbols,re.M))==1, fn
    addresses = {}
    for var,size in [('descr',256),('buffer',65536)]:
        found=re.findall(r'^([0-9a-f]+) b '+var+r'$',symbols,re.M)
        assert len(found)==1, found
        address=int(found[0],16);physical=address&0x1fffffff
        assert physical%256==0 and physical+size<=32*1024*1024, (var,hex(physical))
        addresses[var]=hex(physical)
    if isolated:
        for fn in ('network_poll','tcp_input','xf_http_tick','mp3dec_decode_frame','xf_video_tick','plm_video_decode','Xe_Execute','xf_jvideo_tick'):
            assert not re.search(r' [Tt] '+fn+r'$',symbols,re.M), fn
    report['builds'].append({'name':name,'isolated':isolated,'dma_physical_addresses':addresses})
if a.output:
    a.output.write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
