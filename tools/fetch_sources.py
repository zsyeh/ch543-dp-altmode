#!/usr/bin/env python3
"""Fetch pinned audit inputs for local inspection; not needed to build."""
import hashlib
import json
import pathlib
import urllib.request

root = pathlib.Path(__file__).resolve().parents[1]
manifest = json.loads((root / 'docs/SOURCES.json').read_text())
dest = root / 'sources'
dest.mkdir(exist_ok=True)
for item in manifest['files']:
    path = dest / item['file']
    if path.exists() and hashlib.sha256(path.read_bytes()).hexdigest() == item['sha256']:
        print(item['file'], 'already verified')
        continue
    request = urllib.request.Request(item['url'], headers={'User-Agent': 'Mozilla/5.0'})
    with urllib.request.urlopen(request, timeout=60) as response:
        data = response.read()
    if len(data) != item['bytes'] or hashlib.sha256(data).hexdigest() != item['sha256']:
        raise SystemExit(item['file'] + ': hash mismatch; upstream may have replaced the file')
    path.write_bytes(data)
    print(item['file'], 'downloaded and verified')
