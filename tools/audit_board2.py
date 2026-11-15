#!/usr/bin/env python3
"""Extract only firmware-relevant connectivity/BOM facts from a local EPRO2.

No source archive, images, coordinates, author or account metadata are emitted.
This reads EDA records; it does not validate PCB routing, assembly or signal SI.
"""
import argparse
import hashlib
import json
from pathlib import Path
import zipfile

SELECT = set('U10 U11 U6 PORT1 USB2 PORT3 R1 R3 R70 R5 R7 R52 R2 R4 R71 R72 R6 C41 C42 C43 C44 C45 C46 C47 C48 C49 C53 C54 C55 C56 C57'.split())


def extract(path):
    raw = path.read_bytes()
    with zipfile.ZipFile(path) as archive:
        names = [n for n in archive.namelist() if n.endswith('.epru')]
        if len(names) != 1:
            raise ValueError('expected one .epru document')
        lines = archive.read(names[0]).decode('utf-8-sig').splitlines()
    docs = []
    records = None
    for line in lines:
        if '||' not in line:
            continue
        head, body = line.split('||', 1)
        try:
            h = json.loads(head)
        except ValueError:
            continue
        if h.get('type') == 'BLOB':
            continue
        try:
            b = json.loads(body.rstrip('|')) if body.rstrip('|') else None
        except ValueError:
            continue
        if h.get('type') == 'DOCHEAD':
            kind = b.get('docType') if b else None
            records = {} if kind in ('PCB', 'SCH_PAGE') else None
            if records is not None:
                docs.append((kind, records))
        elif records is not None and 'id' in h:
            key = h['id']
            if h.get('ticket', 0) >= records.get(key, ({'ticket': -1}, None))[0].get('ticket', 0):
                records[key] = (h, b)  # including tombstones
    result = {'source_sha256': hashlib.sha256(raw).hexdigest(), 'components': {}}
    bom = {}
    for kind, records in docs:
        attrs, pads = {}, {}
        for h, b in records.values():
            if b is None:
                continue
            if h['type'] == 'ATTR':
                attrs.setdefault(b.get('parentId'), {})[b['key']] = b.get('value')
            elif h['type'] == 'PAD_NET':
                key = json.loads(h['id'])
                pads.setdefault(key[1], {})[key[2]] = b['padNet']
        for key, a in attrs.items():
            name = a.get('Designator')
            if name not in SELECT:
                continue
            if kind == 'PCB':
                result['components'][name] = {'pads': pads.get(key, {})}
            elif 'Add into BOM' in a:
                bom[name] = a['Add into BOM']
    for name, flag in bom.items():
        if name in result['components']:
            result['components'][name]['schematic_add_into_bom'] = flag
    if not {'U10', 'U11', 'PORT1', 'USB2'} <= result['components'].keys():
        raise ValueError('missing expected Board2 parts')
    return result


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('archive', type=Path)
    p.add_argument('output', type=Path)
    args = p.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(extract(args.archive), indent=2, sort_keys=True) + '\n')
