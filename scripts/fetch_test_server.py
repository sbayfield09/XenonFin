#!/usr/bin/env python3
"""Download checksum-verified official Jellyfin 10.10.7 server/web layers.
Used only for isolated integration tests; no Docker daemon or root required.
"""
import hashlib
import json
import pathlib
import sys
import tarfile
import urllib.request
import copy

MANIFEST = 'sha256:3b38dae4c3ddd6ebc7378538fba4d3f314070ebefbdb3d688166b7c8658fb123'
BASE = 'https://registry-1.docker.io/v2/jellyfin/jellyfin/'

def main():
    dest = pathlib.Path(sys.argv[1] if len(sys.argv)>1 else '/workspace/setup/jellyfin-server').resolve()
    dest.mkdir(parents=True,exist_ok=True)
    cache = dest.parent / 'oci-cache'; cache.mkdir(exist_ok=True)
    def fetch(url,headers=None): return urllib.request.urlopen(urllib.request.Request(url,headers=headers or {}),timeout=120)
    token = json.load(fetch('https://auth.docker.io/token?service=registry.docker.io&scope=repository:jellyfin/jellyfin:pull'))['token']
    headers = {'Authorization':'Bearer '+token,'Accept':'application/vnd.docker.distribution.manifest.v2+json'}
    raw = fetch(BASE+'manifests/'+MANIFEST,headers).read()
    if hashlib.sha256(raw).hexdigest()!=MANIFEST.split(':')[1]: raise RuntimeError('Manifest checksum mismatch')
    manifest = json.loads(raw)
    for i in (6,7):
        layer = manifest['layers'][i]; digest = layer['digest'].split(':')[1]; archive = cache/(digest+'.tar.gz')
        if not archive.exists():
            print('Downloading official Jellyfin layer',i,layer['size'],'bytes',flush=True)
            tmp = archive.with_suffix('.partial')
            with fetch(BASE+'blobs/'+layer['digest'],headers) as r, tmp.open('wb') as w:
                while block := r.read(1024*1024): w.write(block)
            tmp.rename(archive)
        with archive.open('rb') as r:
            if hashlib.file_digest(r,'sha256').hexdigest()!=digest:
                archive.unlink();raise RuntimeError('Layer checksum mismatch')
        with tarfile.open(archive) as t:
            members=[]
            for entry in t:
                name=entry.name.removeprefix('./')
                if name.startswith('jellyfin/'):
                    entry=copy.copy(entry);entry.name=name[len('jellyfin/'):]
                    if entry.name: members.append(entry)
            t.extractall(dest,members=members,filter='data')
        print('Verified and extracted layer',i,flush=True)
    print('Test server installed to',dest)

if __name__=='__main__': main()
