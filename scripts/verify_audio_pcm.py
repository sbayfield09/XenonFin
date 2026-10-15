#!/usr/bin/env python3
"""Export the native generator's actual bytes as WAV references and verify PCM."""
import json
import math
from pathlib import Path
import struct
import subprocess
import wave
ROOT = Path(__file__).resolve().parents[1]
out = ROOT/'build/audio-fixtures'
out.mkdir(parents=True,exist_ok=True)
probe = ROOT/'build/native-tests/sound-diag-tests'
subprocess.run([str(probe),str(out)],check=True)
cases = [('440-stereo',440,440),('880-stereo',880,880),('1000-stereo',1000,1000),('silence',0,0),('440-left',440,0),('440-right',0,440),('440-left-880-right',440,880)]
report={'physical_xbox_tested':False,'rate':48000,'bits':16,'channels':2,'interleaved':'L,R','byte_order':'little','runs':[]}
for i,(name,left,right) in enumerate(cases):
    raw=(out/f'case-{i}.s16le').read_bytes()
    assert len(raw)==48000*4
    samples=struct.unpack('<96000h',raw)
    crossings=[]
    for ch,hz in enumerate((left,right)):
        channel=samples[ch::2]
        for frame,sample in enumerate(channel):
            expected=math.floor(6000*math.sin(2*math.pi*hz*frame/48000)+.5) if hz else 0
            assert sample==expected,(name,ch,frame,sample,expected)
        positive=sum(a<=0<b for a,b in zip(channel,channel[1:]))
        assert positive==hz,(name,ch,positive,hz)
        assert min(channel)==(-6000 if hz else 0) and max(channel)==(6000 if hz else 0)
        crossings.append(positive)
    with wave.open(str(out/(name+'.wav')),'wb') as w:
        w.setnchannels(2);w.setsampwidth(2);w.setframerate(48000);w.writeframes(raw)
    report['runs'].append({'name':name,'left_hz':left,'right_hz':right,'positive_crossings':crossings,'frames':48000,'all_samples_match_sine':True})
(out/'verification.json').write_text(json.dumps(report,indent=2)+'\n')
print('Native packed PCM: all samples and frequency crossings match; 7 WAV references exported. Xbox audible pitch UNTESTED.')
