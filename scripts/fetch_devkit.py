#!/usr/bin/env python3
"""Extract only the Xenon toolchain from pinned Free60 OCI layers.

TLS stays enabled. Every downloaded layer is verified against its registry
SHA-256 before extraction. Does not install or run a container daemon.
"""
import hashlib
import copy
import json
import pathlib
import sys
import tarfile
import urllib.request

MANIFEST = 'sha256:4113852fd3281b50c084f9e26cfb6cdd30b707bb2e7ee676e919dbb7cfbdd24f'
REGISTRY = 'https://registry-1.docker.io/v2/free60/libxenon/'
LAYERS = (8, 17, 19)

def fetch(url, headers=None):
    return urllib.request.urlopen(urllib.request.Request(url, headers=headers or {}), timeout=120)

def main():
    dest = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else '/workspace/setup/devkit').resolve()
    dest.mkdir(parents=True, exist_ok=True)
    cache = dest.parent / 'oci-cache'
    cache.mkdir(exist_ok=True)
    token = json.load(fetch('https://auth.docker.io/token?service=registry.docker.io&scope=repository:free60/libxenon:pull'))['token']
    headers = {'Authorization': 'Bearer ' + token, 'Accept': 'application/vnd.oci.image.manifest.v1+json'}
    raw = fetch(REGISTRY + 'manifests/' + MANIFEST, headers).read()
    if hashlib.sha256(raw).hexdigest() != MANIFEST.split(':')[1]:
        raise RuntimeError('Manifest checksum mismatch')
    manifest = json.loads(raw)
    for i in LAYERS:
        layer = manifest['layers'][i]
        digest = layer['digest'].split(':')[1]
        archive = cache / (digest + '.tar.gz')
        if not archive.exists():
            print('Downloading layer', i, layer['size'], 'bytes', flush=True)
            tmp = archive.with_suffix('.partial')
            with fetch(REGISTRY + 'blobs/' + layer['digest'], headers) as r, tmp.open('wb') as w:
                while block := r.read(1024 * 1024):
                    w.write(block)
            tmp.rename(archive)
        with archive.open('rb') as r:
            actual = hashlib.file_digest(r, 'sha256').hexdigest()
        if actual != digest:
            archive.unlink()
            raise RuntimeError('OCI layer checksum mismatch')
        print('Verified layer', i, flush=True)
        with tarfile.open(archive) as t:
            members = []
            for entry in t:
                name = entry.name.removeprefix('./')
                prefix = 'usr/local/xenon/'
                if name.startswith(prefix):
                    selected = copy.copy(entry)
                    selected.name = name[len(prefix):]
                    if selected.islnk():
                        if not selected.linkname.removeprefix('./').startswith(prefix):
                            raise RuntimeError('Unexpected external hardlink')
                        selected.linkname = selected.linkname.removeprefix('./')[len(prefix):]
                    if selected.name:
                        members.append(selected)
            t.extractall(dest, members=[m for m in members if not m.islnk()], filter='data')
            for member in members:
                if member.islnk():
                    target=dest/member.name
                    if target.is_file() or target.is_symlink(): target.unlink()
            t.extractall(dest, members=[m for m in members if m.islnk()], filter='data')
    print('Toolchain extracted to', dest, flush=True)

if __name__ == '__main__':
    main()
